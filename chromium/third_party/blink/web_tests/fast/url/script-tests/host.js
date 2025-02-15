description("Canonicalization of host names.");

cases = [ 
  // Basic canonicalization, uppercase should be converted to lowercase.
  ["GoOgLe.CoM", "google.com"],
  // Spaces and some other characters should be escaped.
  ["Goo%20 goo%7C|.com", "goo%20%20goo%7C%7C.com"],
  // Exciting different types of spaces!
  ["GOO\u00a0\u3000goo.com", "goo%20%20goo.com"],
  // Other types of space (no-break, zero-width, zero-width-no-break) are
  // name-prepped away to nothing.
  ["GOO\u200b\u2060\ufeffgoo.com", "googoo.com"],
  // Ideographic full stop (full-width period for Chinese, etc.) should be
  // treated as a dot.
  ["www.foo\u3002" + "bar.com", "www.foo.bar.com"],
  // Unicode Noncharacter.
  ["\ufdd0zyx.com", "%EF%B7%90zyx.com"],
  // ...This is the same as previous but with with escaped.
  ["%ef%b7%90zyx.com", "%EF%B7%90zyx.com"],
  // Test name prepping, fullwidth input should be converted to ASCII and NOT
  // IDN-ized. This is "Go" in fullwidth UTF-8/UTF-16.
  ["\uff27\uff4f.com", "go.com"],
  // Test that fullwidth escaped values are properly name-prepped,
  // then converted or rejected.
  // ...%41 in fullwidth = 'A' (also as escaped UTF-8 input)
  ["\uff05\uff14\uff11.com", "a.com"],
  ["%ef%bc%85%ef%bc%94%ef%bc%91.com", "a.com"],
  // ...%00 in fullwidth should fail (also as escaped UTF-8 input)
  ["\uff05\uff10\uff10.com", "%00.com"],
  ["%ef%bc%85%ef%bc%90%ef%bc%90.com", "%00.com"],
  // Basic IDN support, UTF-8 and UTF-16 input should be converted to IDN
  ["\u4f60\u597d\u4f60\u597d", "xn--6qqa088eba"],
  // Mixed UTF-8 and escaped UTF-8 (narrow case) and UTF-16 and escaped
  // UTF-8 (wide case). The output should be equivalent to the true wide
  // character input above).
  ["%E4%BD%A0%E5%A5%BD\u4f60\u597d", "xn--6qqa088eba"],
  // Invalid escaped characters should fail and the percents should be
  // escaped.
  ["%zz%66%a", "%25zzf%25a"],
  // If we get an invalid character that has been escaped.
  ["%25", "%25"],
  ["hello%00", "hello%00"],
  // Escaped numbers should be treated like IP addresses if they are.
  ["%30%78%63%30%2e%30%32%35%30.01", "192.168.0.1"],
  ["%30%78%63%30%2e%30%32%35%30.01%2e", "192.168.0.1"],
  // Invalid escaping should trigger the regular host error handling.
  ["%3g%78%63%30%2e%30%32%35%30%2E.01", "%253gxc0.0250..01"],
  // Something that isn't exactly an IP should get treated as a host and
  // spaces escaped.
  ["192.168.0.1 hello", "192.168.0.1%20hello"],
  // Fullwidth and escaped UTF-8 fullwidth should still be treated as IP.
  // These are "0Xc0.0250.01" in fullwidth.
  ["\uff10\uff38\uff43\uff10\uff0e\uff10\uff12\uff15\uff10\uff0e\uff10\uff11", "192.168.0.1"],
  // Broken IP addresses get marked as such.
  ["192.168.0.257", "192.168.0.257"],
  ["[google.com]", "[google.com]"],
  // Cyrillic letter followed buy ( should return punicode for ( escaped before punicode string was created. I.e.
  // if ( is escaped after punicode is created we would get xn--%28-8tb (incorrect).
  ["\u0442(", "xn--%28-7ed"],
  ["go\\\\@ogle.com","go/@ogle.com"],
  ["go/@ogle.com","go/@ogle.com"],
  ["www.lookout.net::==80::==443::","www.lookout.net::%3D%3D80::%3D%3D443:"],
  ["www.lookout.net::80::443","www.lookout.net::80::443"],
  // From http://eaea.sirdarckcat.net/uritest.html
  ["\\./","./"],
  ["//:@/","/"],
  ["\\google.com/foo","google.com/foo"],
  ["\\\\google.com/foo","google.com/foo"],
  ["//asdf@/","asdf@/"],
  ["//:81",":81"],
  ["://","//"],
  ["c:","c"],
  ["xxxx:","xxxx"],
  [".:.",".:."],
  ["////@google.com/","google.com/"],
  ["@google.com","google.com"]
];

for (var i = 0; i < cases.length; ++i) {
  test_vector = cases[i][0];
  expected_result = cases[i][1];
  shouldBe("canonicalize('http://" + test_vector + "/')",
           "'http://" + expected_result + "/'");
}
