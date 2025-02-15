// DO NOT EDIT! This test has been generated by /html/canvas/tools/gentest.py.
// OffscreenCanvas test in a worker:2d.text.draw.baseline.ideographic
// Description:
// Note:

importScripts("/resources/testharness.js");
importScripts("/html/canvas/resources/canvas-tests.js");

var t = async_test("");
var t_pass = t.done.bind(t);
var t_fail = t.step_func(function(reason) {
    throw reason;
});
t.step(function() {

  var canvas = new OffscreenCanvas(100, 50);
  var ctx = canvas.getContext('2d');

  var f = new FontFace("CanvasTest", "url('/fonts/CanvasTest.ttf')");
  let fonts = (self.fonts ? self.fonts : document.fonts);
  f.load();
  fonts.add(f);
  fonts.ready.then(function() {
      ctx.font = '50px CanvasTest';
      ctx.fillStyle = '#f00';
      ctx.fillRect(0, 0, 100, 50);
      ctx.fillStyle = '#0f0';
      ctx.textBaseline = 'ideographic';
      ctx.fillText('CC', 0, 81.25);
      _assertPixelApprox(canvas, 5,5, 0,255,0,255, 2);
      _assertPixelApprox(canvas, 95,5, 0,255,0,255, 2);
      _assertPixelApprox(canvas, 25,25, 0,255,0,255, 2);
      _assertPixelApprox(canvas, 75,25, 0,255,0,255, 2);
      _assertPixelApprox(canvas, 5,45, 0,255,0,255, 2);
      _assertPixelApprox(canvas, 95,45, 0,255,0,255, 2);
    }).then(t_pass, t_fail);
});
done();
