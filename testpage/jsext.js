// 外链脚本：验证 <script src> 会被导航线程取回并执行。
window.__extLoaded = true;

document.addEventListener('DOMContentLoaded', function () {
  var el = document.getElementById('ext');
  if (el) {
    el.textContent = '外链脚本已执行';
  }
});

document.getElementById('ext2').textContent =
  'ext-sum=' + [10, 20, 30].reduce(function (a, b) { return a + b; }, 0);
