// Debugger.Object.prototype.isModule

const g = newGlobal();
const dbg = Debugger(g);
let count = 0;
dbg.onNewScript = function (script) {
    count += 1;
    assertEq(script.isModule, true);
};
const m = g.parseModule("");
g.instantiateModule(m);
g.evaluateModule(m);
assertEq(count, 1);
