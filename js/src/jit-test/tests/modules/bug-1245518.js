evalInFrame = function(global) {
  dbgGlobal = newGlobal();
  dbg = new dbgGlobal.Debugger();
  return function(upCount, code) {
    dbg.addDebuggee(global);
    frame = dbg.getNewestFrame().older;
    frame.eval(code);
  }
}(this);
m = parseModule(`
  function g() { return this.hours = 0; }
  evalInFrame.call(0, 0, "g()")
`);
instantiateModule(m);
evaluateModule(m);
