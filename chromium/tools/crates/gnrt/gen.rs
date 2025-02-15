// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::*;

use crates::{Epoch, ThirdPartySource, VendoredCrate};
use manifest::*;

use crate::util::{check_exit_ok, check_spawn, check_wait_with_output, create_dirs_if_needed};

use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::path::{Path, PathBuf};
use std::process;

use anyhow::{bail, ensure, format_err, Context, Result};

pub fn generate(args: &clap::ArgMatches, paths: &paths::ChromiumPaths) -> Result<()> {
    if args.get_one::<String>("for-std").is_some() {
        generate_for_std(&args, &paths)
    } else {
        generate_for_third_party(&args, &paths)
    }
}

fn generate_for_third_party(args: &clap::ArgMatches, paths: &paths::ChromiumPaths) -> Result<()> {
    // Traverse our third-party directory to collect the set of vendored crates.
    // Used to generate Cargo.toml [patch] sections, and later to check against
    // `cargo metadata`'s dependency resolution to ensure we have all the crates
    // we need. We sort `crates` for a stable ordering of [patch] sections.
    let source = crates::ThirdPartySource::new(paths.third_party.clone())?;

    let manifest_contents =
        String::from_utf8(fs::read(paths.third_party.join("third_party.toml")).unwrap()).unwrap();
    let third_party_manifest: ThirdPartyManifest =
        toml::de::from_str(&manifest_contents).context("Could not parse third_party.toml")?;

    let mut crates_metadata = HashMap::<VendoredCrate, gn::PerCrateMetadata>::new();

    // Collect the Chromium-specific fields from third_party.toml and store them
    // in `crates_metadata`.
    for (dep_name, dep_spec, dep_vis) in [
        (&third_party_manifest.testonly_dependencies, crates::Visibility::TestOnlyAndThirdParty),
        (&third_party_manifest.dependencies, crates::Visibility::Public),
    ]
    .into_iter()
    .flat_map(|(list, vis)| list.iter().map(move |(name, spec)| (name, spec, vis)))
    {
        let full_dep: ThirdPartyFullDependency = dep_spec.clone().into_full();

        let version_req = full_dep.version.to_version_req();
        let crate_id = source.find_match(dep_name, &version_req).ok_or_else(|| {
            format_err!(
                "{dep_name} {version_req} was requested in third_party.toml \
                but not present in vendored crates"
            )
        })?;

        crates_metadata.insert(
            crate_id,
            gn::PerCrateMetadata {
                build_script_outputs: full_dep.build_script_outputs,
                gn_variables: full_dep.gn_variables_lib,
                visibility: if full_dep.allow_first_party_usage {
                    dep_vis
                } else {
                    crates::Visibility::ThirdParty
                },
            },
        );
    }

    // Rebind as immutable.
    let (third_party_manifest, crates_metadata) = (third_party_manifest, crates_metadata);

    // Generate a fake root Cargo.toml for dependency resolution.
    let cargo_manifest = generate_fake_cargo_toml(third_party_manifest, source.cargo_patches());

    if args.get_flag("output-cargo-toml") {
        println!("{}", toml::ser::to_string(&cargo_manifest).unwrap());
        return Ok(());
    }

    // Create a fake package: Cargo.toml and an empty main.rs. This allows cargo
    // to construct a full dependency graph as if Chrome were a cargo package.
    write!(
        io::BufWriter::new(fs::File::create(paths.third_party.join("Cargo.toml")).unwrap()),
        "# {}\n\n{}",
        AUTOGENERATED_FILE_HEADER,
        toml::to_string(&cargo_manifest).unwrap()
    )
    .unwrap();
    create_dirs_if_needed(&paths.third_party.join("src")).unwrap();
    write!(
        io::BufWriter::new(fs::File::create(paths.third_party.join("src/main.rs")).unwrap()),
        "// {}",
        AUTOGENERATED_FILE_HEADER
    )
    .unwrap();

    // Run `cargo metadata` and process the output to get a list of crates we
    // depend on.
    let mut command = cargo_metadata::MetadataCommand::new();
    if let Some(cargo_path) = args.get_one::<String>("cargo-path") {
        command.cargo_path(cargo_path);
    }
    if let Some(rustc_path) = args.get_one::<String>("rustc-path") {
        command.env("RUSTC", rustc_path);
    }

    command.current_dir(&paths.third_party);
    let dependencies = deps::collect_dependencies(&command.exec().unwrap(), None, None);

    // Compare cargo's dependency resolution with the crates we have on disk. We
    // want to ensure:
    // * Each resolved dependency matches with a crate we discovered (no missing
    //   deps).
    // * Each discovered crate matches with a resolved dependency (no unused
    //   crates).
    let mut has_error = false;
    let present_crates = source.present_crates();

    // Construct the set of requested third-party crates, ensuring we don't have
    // duplicate epochs. For example, if we resolved two versions of a
    // dependency with the same major version, we cannot continue.
    let mut req_crates = HashMap::<(String, Epoch), Version>::new();
    for dep in &dependencies {
        let crate_id = dep.crate_id();
        let epoch = crates::Epoch::from_version(&crate_id.version);
        if let Some(conflict) =
            req_crates.insert((crate_id.name.clone(), epoch), crate_id.version.clone())
        {
            bail!(
                "Cargo resolved two different package versions of the same epoch: {} {} vs {}",
                dep.package_name,
                dep.version,
                conflict
            );
        }

        if !present_crates.contains_key(&crate_id) {
            has_error = true;
            println!("Missing dependency: {} {}", dep.package_name, dep.version);
            for edge in dep.dependency_path.iter() {
                println!("    {edge}");
            }
        } else if !dep.is_local {
            // Transitive deps may be requested with version requirements stricter than
            // ours: e.g. 1.57 instead of just major version 1. If the version we have
            // checked in, e.g. 1.56, has the same epoch but doesn't meet the version
            // requirement, the symptom is Cargo will resolve the dependency to an
            // upstream source instead of our local path. We must detect this case to
            // ensure correctness.
            has_error = true;
            println!(
                "Resolved {} {} to an upstream source. The requested version \
                 likely has the same epoch as the discovered crate but \
                 something has a more stringent version requirement.",
                dep.package_name, dep.version
            );
            println!("    Resolved version: {}", dep.version);
        }
    }
    let req_crates = req_crates;

    for present_crate in present_crates.keys() {
        if !req_crates.contains_key(&(
            present_crate.name.clone(),
            Epoch::from_version(&present_crate.version),
        )) {
            has_error = true;
            println!("Unused crate: {present_crate}");
        }
    }

    ensure!(!has_error, "Dependency resolution failed");

    let build_files: HashMap<VendoredCrate, gn::BuildFile> =
        gn::build_files_from_chromium_deps(&dependencies, &paths, &crates_metadata, |crate_id| {
            // A missing crate should have been detected above, so unwrap.
            present_crates.get(&crate_id).unwrap()
        });

    // Before modifying anything make sure we have a one-to-one mapping of
    // discovered crates and build file data.
    for crate_id in build_files.keys() {
        // This shouldn't happen, but check anyway in case we have a strange
        // logic error above.
        assert!(present_crates.contains_key(crate_id));
    }

    for crate_id in present_crates.keys() {
        if !build_files.contains_key(crate_id) {
            println!("Error: discovered crate {crate_id}, but no build file was generated.");
            has_error = true;
        }
    }

    ensure!(!has_error, "Generated build rules don't match input dependencies");

    // Wipe all previous BUILD.gn files. If we fail, we don't want to leave a
    // mix of old and new build files.
    for build_file in present_crates.iter().map(|(crate_id, _)| build_file_path(&crate_id, &paths))
    {
        if build_file.exists() {
            fs::remove_file(&build_file).unwrap();
        }
    }

    // Generate build files, wiping the previous ones so we don't have any stale
    // build rules.
    for (crate_id, _) in present_crates.iter() {
        let build_file_path = build_file_path(&crate_id, &paths);
        let build_file_data = match build_files.get(crate_id) {
            Some(build_file) => build_file,
            None => panic!("missing build data for {crate_id}"),
        };

        write_build_file(&build_file_path, build_file_data).unwrap();
    }

    Ok(())
}

fn generate_for_std(args: &clap::ArgMatches, paths: &paths::ChromiumPaths) -> Result<()> {
    // Load config file, which applies rustenv and cfg flags to some std crates.
    let config_file_contents = std::fs::read_to_string(paths.std_config_file).unwrap();
    let config: config::BuildConfig = toml::de::from_str(&config_file_contents).unwrap();

    // The Rust source tree, containing the standard library and vendored
    // dependencies.
    let rust_src_root = {
        let for_std_value = args.get_one::<String>("for-std").unwrap();
        if !for_std_value.is_empty() {
            for_std_value.clone()
        } else {
            paths.rust_src_installed.to_string_lossy().to_string()
        }
    };
    println!("Generating stdlib GN rules from {}", rust_src_root);

    let cargo_config = std::fs::read_to_string(paths.std_fake_root_config_template)
        .unwrap()
        .replace("RUST_SRC_ROOT", &rust_src_root);
    std::fs::write(
        paths.strip_template(paths.std_fake_root_config_template).unwrap(),
        cargo_config,
    )
    .unwrap();

    let cargo_toml = std::fs::read_to_string(paths.std_fake_root_cargo_template)
        .unwrap()
        .replace("RUST_SRC_ROOT", &rust_src_root);
    std::fs::write(paths.strip_template(paths.std_fake_root_cargo_template).unwrap(), cargo_toml)
        .unwrap();
    // Convert the `rust_src_root` to a Path hereafter.
    let rust_src_root = paths.root.join(Path::new(&rust_src_root));

    // Run `cargo metadata` from the std package in the Rust source tree (which
    // is a workspace).
    let mut command = cargo_metadata::MetadataCommand::new();
    if let Some(cargo_path) = args.get_one::<String>("cargo-path") {
        command.cargo_path(cargo_path);
    }
    if let Some(rustc_path) = args.get_one::<String>("rustc-path") {
        command.env("RUSTC", rustc_path);
    }

    command.current_dir(paths.std_fake_root);

    // The Cargo.toml files in the Rust toolchain may use nightly Cargo
    // features, but the cargo binary is beta. This env var enables the
    // beta cargo binary to allow nightly features anyway.
    // https://github.com/rust-lang/rust/commit/2e52f4deb0544480b6aefe2c0cc1e6f3c893b081
    command.env("RUSTC_BOOTSTRAP", "1");

    // Delete the Cargo.lock if it exists.
    let mut std_fake_root_cargo_lock = paths.std_fake_root.to_path_buf();
    std_fake_root_cargo_lock.push("Cargo.lock");
    if let Err(e) = std::fs::remove_file(std_fake_root_cargo_lock) {
        match e.kind() {
            // Ignore if it already doesn't exist.
            std::io::ErrorKind::NotFound => (),
            _ => panic!("io error while deleting Cargo.lock: {e}"),
        }
    }

    // Use offline to constrain dependency resolution to those in the Rust src
    // tree and vendored crates. Ideally, we'd use "--locked" and use the
    // upstream Cargo.lock, but this is not straightforward since the rust-src
    // component is not a full Cargo workspace. Since the vendor dir we package
    // is generated with "--locked", the outcome should be the same.
    command.other_options(vec!["--offline".to_string()]);

    // Compute the set of crates we need to build to build libstd. Note this
    // contains a few kinds of entries:
    // * Rust workspace packages (e.g. core, alloc, std, unwind, etc)
    // * Non-workspace packages supplied in Rust source tree (e.g. stdarch)
    // * Vendored third-party crates (e.g. compiler_builtins, libc, etc)
    // * rust-std-workspace-* shim packages which direct std crates.io
    //   dependencies to the correct lib{core,alloc,std} when depended on by the
    //   Rust codebase (see
    //   https://github.com/rust-lang/rust/tree/master/library/rustc-std-workspace-core)
    let mut dependencies = deps::collect_dependencies(
        &command.exec().unwrap(),
        Some(vec![config.resolve.root.clone()]),
        None,
    );

    // Remove dev dependencies since tests aren't run. Also remove build deps
    // since we configure flags and env vars manually. Include the root
    // explicitly since it doesn't get a dependency_kinds entry.
    dependencies.retain(|dep| dep.dependency_kinds.contains_key(&deps::DependencyKind::Normal));

    dependencies.sort_unstable_by(|a, b| {
        a.package_name.cmp(&b.package_name).then(a.version.cmp(&b.version))
    });
    for dep in dependencies.iter_mut() {
        // Rehome stdlib deps from the `rust_src_root` to where they will be installed
        // in the Chromium checkout.
        let gn_prefix = paths.root.join(paths.rust_src_installed);
        match dep.lib_target.as_mut() {
            Some(lib) => {
                ensure!(
                    lib.root.canonicalize().unwrap().starts_with(&rust_src_root),
                    "Found dependency that was not locally available: {} {}\n{:?}",
                    dep.package_name,
                    dep.version,
                    dep
                );

                if let Ok(remain) = lib.root.canonicalize().unwrap().strip_prefix(&rust_src_root) {
                    lib.root = gn_prefix.join(remain);
                }
            }
            None => (),
        }
        match dep.build_script.as_mut() {
            Some(path) => {
                if let Ok(remain) = path.canonicalize().unwrap().strip_prefix(&rust_src_root) {
                    *path = gn_prefix.join(remain);
                }
            }
            None => (),
        }
    }

    let third_party_deps = dependencies.iter().filter(|dep| !dep.is_local).collect::<Vec<_>>();

    // Check that all resolved third party deps are available. First, collect
    // the set of third-party dependencies vendored in the Rust source package.
    let vendored_crates: HashMap<VendoredCrate, manifest::CargoPackage> =
        crates::collect_std_vendored_crates(&rust_src_root.join(paths.rust_src_vendor_subdir))
            .unwrap()
            .into_iter()
            .collect();

    // Collect vendored dependencies, and also check that all resolved
    // dependencies point to our Rust source package. Build rules will be
    // generated for these crates separately from std, alloc, and core which
    // need special treatment.
    for dep in third_party_deps.iter() {
        // Only process deps with a library target: we are producing build rules
        // for the standard library, so transitive binary dependencies don't
        // make sense.
        if dep.lib_target.is_none() {
            continue;
        }

        vendored_crates
            .get_key_value(&VendoredCrate {
                name: dep.package_name.clone(),
                version: dep.version.clone(),
            })
            .ok_or_else(|| {
                format_err!(
                    "Resolved dependency does not match any vendored crate: {} {}",
                    dep.package_name,
                    dep.version
                )
            })?;
    }

    let build_file = gn::build_file_from_std_deps(dependencies.iter(), paths, &config);
    write_build_file(&paths.std_build.join("BUILD.gn"), &build_file).unwrap();

    Ok(())
}

fn build_file_path(crate_id: &VendoredCrate, paths: &paths::ChromiumPaths) -> PathBuf {
    let mut path = paths.root.clone();
    path.push(&paths.third_party);
    path.push(ThirdPartySource::build_path(&crate_id));
    path.push("BUILD.gn");
    path
}

fn write_build_file(path: &Path, build_file: &gn::BuildFile) -> Result<()> {
    let cmd_name = "gn format";
    let output_handle = fs::File::create(path)
        .with_context(|| format!("Could not create GN output file {}", path.to_string_lossy()))?;

    // Spawn a child process to format GN rules. The formatted GN is written to
    // the file `output_handle`.
    let mut child = check_spawn(
        &mut process::Command::new("gn")
            .arg("format")
            .arg("--stdin")
            .stdin(process::Stdio::piped())
            .stdout(output_handle),
        cmd_name,
    )?;

    write!(io::BufWriter::new(child.stdin.take().unwrap()), "{}", build_file.display())
        .context("Failed to write to GN format process")?;
    check_exit_ok(&check_wait_with_output(child, cmd_name)?, cmd_name)
}

/// A message prepended to autogenerated files. Notes this tool generated it and
/// not to edit directly.
static AUTOGENERATED_FILE_HEADER: &'static str = "!!! DO NOT EDIT -- Autogenerated by gnrt from third_party.toml. Edit that file instead. See tools/crates/gnrt.";
