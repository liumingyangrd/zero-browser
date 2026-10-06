// 解释器自测：每个用例打一行 PASS/FAIL，最后打 SUMMARY。
// 跑法：js_probe.exe tools\js_selftest.js
// 退出码由 js_probe 返回（有未捕获异常即非 0），FAIL 行用于人工/脚本核对。

var passed = 0;
var failed = 0;

function eq(actual, expected, name) {
  if (actual === expected) {
    passed = passed + 1;
  } else {
    failed = failed + 1;
    console.log("FAIL " + name + ": got " + actual + " want " + expected);
  }
}

function ok(cond, name) {
  eq(!!cond, true, name);
}

// --- 基本运算与类型 ---
eq(1 + 2 * 3, 7, "precedence");
eq((1 + 2) * 3, 9, "parens");
eq(7 % 3, 1, "modulo");
eq(2 ** 10, 1024, "power");
eq(7 / 2, 3.5, "divide");
eq(-5 + 3, -2, "unary minus");
eq(typeof 1, "number", "typeof number");
eq(typeof "a", "string", "typeof string");
eq(typeof undefined, "undefined", "typeof undefined");
eq(typeof null, "object", "typeof null");
eq(typeof eq, "function", "typeof function");
eq(typeof notDeclaredAnywhere, "undefined", "typeof undeclared");
eq(1 == "1", true, "loose equal");
eq(1 === "1", false, "strict equal");
eq(null == undefined, true, "null == undefined");
eq("" + 12, "12", "number to string");
eq("3" * "4", 12, "string to number");
eq(true && "x", "x", "logical and value");
eq(false || "y", "y", "logical or value");
eq(1 < 2, true, "less");
eq("a" < "b", true, "string compare");
eq(5 & 3, 1, "bitand");
eq(1 << 4, 16, "shift");

// --- 变量与作用域 ---
var v1 = 10;
let l1 = 20;
const c1 = 30;
eq(v1 + l1 + c1, 60, "var/let/const");
function scopeTest() {
  var inner = "in";
  return inner;
}
eq(scopeTest(), "in", "function scope");
eq(typeof inner, "undefined", "var stays in function");

// --- 函数：声明 / 表达式 / 箭头 / 闭包 / 递归 / 提升 ---
eq(hoisted(), "hoisted-ok", "function hoisting");
function hoisted() { return "hoisted-ok"; }

var fe = function (x) { return x * 2; };
eq(fe(21), 42, "function expression");

var arrow = (a, b) => a + b;
eq(arrow(2, 3), 5, "arrow function");

var simple = x => x + 1;
eq(simple(1), 2, "single-param arrow");

function counter() {
  var n = 0;
  return function () { n = n + 1; return n; };
}
var c = counter();
c(); c();
eq(c(), 3, "closure state");

function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }
eq(fact(5), 120, "recursion");

function withDefault(a, b) { return b === undefined ? a : a + b; }
eq(withDefault(1), 1, "missing arg is undefined");

function argCount() { return arguments.length; }
eq(argCount(1, 2, 3), 3, "arguments array");

function outer() { var x = 1; function inner() { return x; } return inner(); }
eq(outer(), 1, "nested function");

// --- 对象 ---
var obj = { a: 1, "b": 2, c: 3 };
eq(obj.a + obj["b"] + obj.c, 6, "object literal access");
obj.d = 4;
eq(obj.d, 4, "object property add");
eq(Object.keys({ x: 1, y: 2 }).length, 2, "Object.keys");
var merged = Object.assign({}, { a: 1 }, { b: 2 });
eq(merged.a + merged.b, 3, "Object.assign");
eq(({ k: 1 }).hasOwnProperty("k"), true, "hasOwnProperty");
var method = { n: 5, get: function () { return this.n; } };
eq(method.get(), 5, "this in method");
var computedKey = "dyn";
var dynObj = {};
dynObj[computedKey] = 9;
eq(dynObj.dyn, 9, "computed property");

// --- 数组 ---
var arr = [1, 2, 3];
eq(arr.length, 3, "array length");
arr.push(4);
eq(arr.length, 4, "push");
eq(arr.pop(), 4, "pop");
eq(arr.join("-"), "1-2-3", "join");
eq(arr.indexOf(2), 1, "indexOf");
eq(arr.slice(1).length, 2, "slice");
eq(arr.map(function (x) { return x * 2; }).join(","), "2,4,6", "map");
eq(arr.filter(function (x) { return x > 1; }).join(","), "2,3", "filter");
eq(arr.reduce(function (a, b) { return a + b; }, 0), 6, "reduce");
eq(arr.some(function (x) { return x === 3; }), true, "some");
eq(arr.every(function (x) { return x > 0; }), true, "every");
eq(arr.find(function (x) { return x > 1; }), 2, "find");
var unsorted = [3, 1, 2];
unsorted.sort(function (a, b) { return a - b; });
eq(unsorted.join(""), "123", "sort with comparator");
eq([1, 2].concat([3]).join(","), "1,2,3", "concat");
eq(Array.isArray([]), true, "Array.isArray");
eq(Array.isArray({}), false, "Array.isArray object");
eq(Array.from("ab").join(","), "a,b", "Array.from string");
var sp = [1, 2, 3, 4];
sp.splice(1, 2);
eq(sp.join(","), "1,4", "splice");
eq([1, 2, 3].slice.call([9, 8, 7]).join(","), "9,8,7", "Array.prototype.slice.call");
var nested = [[1, 2], [3]];
eq(nested[1][0], 3, "nested array");

// --- 控制流 ---
var sum = 0;
for (var i = 0; i < 5; i++) { sum += i; }
eq(sum, 10, "for loop");

var ws = 0;
var wi = 0;
while (wi < 3) { ws += wi; wi++; }
eq(ws, 3, "while loop");

var ds = 0;
var di = 0;
do { ds += 10; di++; } while (di < 2);
eq(ds, 20, "do while");

var fi = "";
for (var k in { a: 1, b: 2 }) { fi += k; }
eq(fi, "ab", "for in");

var fo = "";
var list = ["x", "y"];
for (var item of list) { fo += item; }
eq(fo, "xy", "for of array");

var fo2 = "";
for (var ch of "ab") { fo2 += ch; }
eq(fo2, "ab", "for of string");

var brk = 0;
for (var bi = 0; bi < 10; bi++) { if (bi === 3) break; brk++; }
eq(brk, 3, "break");

var conts = 0;
for (var ci = 0; ci < 5; ci++) { if (ci % 2 === 0) continue; conts++; }
eq(conts, 2, "continue");

var sw = "";
switch (2) {
  case 1: sw = "one"; break;
  case 2: sw = "two"; break;
  default: sw = "other";
}
eq(sw, "two", "switch case");

var swd = "";
switch (99) {
  case 1: swd = "one"; break;
  default: swd = "def";
}
eq(swd, "def", "switch default");

// --- 异常 ---
var caught = "";
try {
  throw new Error("boom");
} catch (e) {
  caught = e.message;
}
eq(caught, "boom", "throw/catch message");

var finallyRan = false;
try {
  throw "plain";
} catch (e) {
  caught = e;
} finally {
  finallyRan = true;
}
eq(caught, "plain", "throw string");
eq(finallyRan, true, "finally runs");

var typeCaught = "";
try {
  null.x;
} catch (e) {
  typeCaught = "caught";
}
eq(typeCaught, "caught", "null property access throws");

var nestedCatch = "";
try {
  try {
    throw new Error("inner");
  } finally {
    nestedCatch += "f";
  }
} catch (e) {
  nestedCatch += e.message;
}
eq(nestedCatch, "finner", "nested finally + outer catch");

var errStr = String(new Error("msg"));
ok(errStr.indexOf("msg") >= 0, "Error string contains message");

// --- 内置对象 ---
eq(Math.floor(3.7), 3, "Math.floor");
eq(Math.ceil(3.1), 4, "Math.ceil");
eq(Math.round(2.5), 3, "Math.round");
eq(Math.max(1, 5, 3), 5, "Math.max");
eq(Math.min(4, 2), 2, "Math.min");
eq(Math.abs(-3), 3, "Math.abs");
eq(Math.pow(2, 3), 8, "Math.pow");
ok(Math.PI > 3.14 && Math.PI < 3.15, "Math.PI");
ok(Math.random() >= 0 && Math.random() < 1, "Math.random range");

eq(parseInt("42px"), 42, "parseInt");
eq(parseFloat("3.5x"), 3.5, "parseFloat");
eq(parseInt("ff", 16), 255, "parseInt radix");
eq(isNaN(NaN), true, "isNaN");
eq(isFinite(1), true, "isFinite");
eq(String(123), "123", "String()");
eq(Number("4.5"), 4.5, "Number()");
eq(Boolean(0), false, "Boolean()");
eq(Boolean("x"), true, "Boolean() string");
eq((255).toString(16), "ff", "Number.toString radix");
eq((1.005).toFixed(2), "1.00", "toFixed");

eq("abc".length, 3, "string length");
eq("abc".charAt(1), "b", "charAt");
eq("abc".indexOf("c"), 2, "string indexOf");
eq("abc".toUpperCase(), "ABC", "toUpperCase");
eq("ABC".toLowerCase(), "abc", "toLowerCase");
eq("  x  ".trim(), "x", "trim");
eq("a,b,c".split(",").length, 3, "split");
eq("abc".slice(1), "bc", "string slice");
eq("abc".substring(1, 3), "bc", "substring");
eq("abc".substr(1, 1), "b", "substr");
eq("a-b".replace("-", "+"), "a+b", "replace");
eq("aaa".replaceAll("a", "b"), "bbb", "replaceAll");
eq("ab".concat("c"), "abc", "concat");
eq("ab".repeat(2), "abab", "repeat");
eq("5".padStart(2, "0"), "05", "padStart");
eq("abc".includes("b"), true, "includes");
eq("abc".startsWith("ab"), true, "startsWith");
eq("abc".endsWith("bc"), true, "endsWith");
eq(`static template`, "static template", "template literal");

eq(JSON.stringify({ a: 1 }).indexOf('"a"') >= 0, true, "JSON.stringify");
eq(JSON.parse('{"a":[1,2]}').a[1], 2, "JSON.parse");
eq(JSON.parse("[1,2,3]").length, 3, "JSON.parse array");
eq(JSON.stringify([1, "a", true]), '[1,"a",true]', "JSON round trip");

var d = new Date(1700000000000);
eq(typeof d.getTime(), "number", "Date getTime");
eq(d.getTime(), 1700000000000, "Date value preserved");
ok(Date.now() > 1600000000000, "Date.now");

function Point(x, y) { this.x = x; this.y = y; }
Point.prototype.sum = function () { return this.x + this.y; };
var p = new Point(1, 2);
eq(p.sum(), 3, "custom constructor + prototype");
ok(p instanceof Point, "instanceof custom");
ok([] instanceof Array, "instanceof Array");
ok({} instanceof Object, "instanceof Object");

var o2 = Object.create({ inherited: 7 });
eq(o2.inherited, 7, "Object.create prototype");

// --- 运算符补充 ---
eq(void 0, undefined, "void");
eq(delete obj.a, true, "delete");
eq(obj.a, undefined, "delete removed");
var inc = 5;
eq(inc++, 5, "postfix ++ returns old");
eq(inc, 6, "postfix ++ applied");
eq(++inc, 7, "prefix ++ returns new");
var acc = 1;
acc += 4; acc *= 2;
eq(acc, 10, "compound assign");
var t = true ? "y" : "n";
eq(t, "y", "ternary");
eq([1, 2].length ? "t" : "f", "t", "truthy array");
var chain = { a: { b: { c: 42 } } };
eq(chain.a.b.c, 42, "deep member");

console.log("SUMMARY passed=" + passed + " failed=" + failed);
if (failed > 0) {
  throw new Error("selftest failed: " + failed);
}
