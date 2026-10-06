"""把 C++ 源码里的 // 行注释与 /* */ 块注释剥掉，用于生成"公开版"源码。

为什么要写成词法感知的，而不是正则替换：
- 字符串字面量里就有 `//`：本项目内置页是 R"HTML(...)" 原始字符串，内容里写着
  browser://settings、http://、https:// —— 正则会把它们当注释删掉，源码当场损坏。
- 字符字面量 `'/'`、转义、行尾反斜杠续行、原始字符串的自定义分隔符都要照顾到。

另一个刻意的设计：**只删注释本身，不删它占的换行**。
整行注释被删成空行，跨行块注释替换成等量的换行符 —— 于是剥离前后的
**行号完全一致**，diff 能对齐，出问题时也好对照。

用法:
    python tools/strip_comments.py <源目录> <目标目录>     # 剥离 *.cpp / *.h
    python tools/strip_comments.py --check <文件>          # 只打印统计，不写文件
"""
import os
import sys


def strip_cpp_comments(text):
    """返回 (剥离后的文本, 删除的行注释数, 删除的块注释数)。"""
    out = []
    i = 0
    n = len(text)
    line_comments = 0
    block_comments = 0

    while i < n:
        c = text[i]

        # ---- // 行注释（含行尾反斜杠续行）----
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            line_comments += 1
            j = i
            while True:
                eol = text.find("\n", j)
                if eol == -1:
                    j = n
                    break
                # 数一数换行前有几个连续反斜杠：奇数个才续行
                k = eol - 1
                backslashes = 0
                while k >= 0 and text[k] == "\\":
                    backslashes += 1
                    k -= 1
                if backslashes % 2 == 1:
                    j = eol + 1
                    continue
                j = eol
                break
            i = j  # 注意：不吞掉这个 '\n'，行号才不变
            continue

        # ---- /* */ 块注释 ----
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            block_comments += 1
            if end == -1:
                # 未闭合：按到文件末尾处理，保留其中的换行
                out.append("\n" * text.count("\n", i))
                i = n
            else:
                body = text[i:end + 2]
                out.append("\n" * body.count("\n"))
                i = end + 2
            continue

        # ---- 原始字符串 R"delim(...)delim" ----
        if c == "R" and i + 1 < n and text[i + 1] == '"':
            j = i + 2
            delim = []
            while j < n and text[j] != "(":
                delim.append(text[j])
                j += 1
            if j < n:
                closer = ")" + "".join(delim) + '"'
                end = text.find(closer, j + 1)
                if end == -1:
                    out.append(text[i:])
                    i = n
                    continue
                end += len(closer)
                out.append(text[i:end])
                i = end
                continue

        # ---- 普通字符串 / 字符字面量 ----
        if c == '"' or c == "'":
            quote = c
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == quote:
                    j += 1
                    break
                if text[j] == "\n":
                    break  # 未闭合，别一路吞下去
                j += 1
            out.append(text[i:j])
            i = j
            continue

        out.append(c)
        i += 1

    return "".join(out), line_comments, block_comments


def strip_tree(src_dir, dst_dir):
    exts = (".cpp", ".h")
    files = 0
    total_line = 0
    total_block = 0
    for root, _dirs, names in os.walk(src_dir):
        for name in sorted(names):
            if not name.endswith(exts):
                continue
            src_path = os.path.join(root, name)
            rel = os.path.relpath(src_path, src_dir)
            dst_path = os.path.join(dst_dir, rel)
            os.makedirs(os.path.dirname(dst_path), exist_ok=True)
            with open(src_path, "rb") as fh:
                raw = fh.read()
            stripped, lc, bc = strip_cpp_comments(raw.decode("utf-8"))
            with open(dst_path, "w", encoding="utf-8", newline="") as fh:
                fh.write(stripped)
            files += 1
            total_line += lc
            total_block += bc
            # 行数必须一致：剥离只删注释文本，不动换行
            if stripped.count("\n") != raw.decode("utf-8").count("\n"):
                print("  !! line count changed in %s" % rel)
            print("  %-24s -%3d line, -%2d block" % (rel, lc, bc))
    print("stripped %d files: %d line comments, %d block comments"
          % (files, total_line, total_block))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        with open(sys.argv[2], "rb") as fh:
            text = fh.read().decode("utf-8")
        s, lc, bc = strip_cpp_comments(text)
        print("line comments=%d block comments=%d lines before=%d after=%d"
              % (lc, bc, text.count("\n"), s.count("\n")))
    elif len(sys.argv) == 3:
        strip_tree(sys.argv[1], sys.argv[2])
    else:
        print(__doc__)
        sys.exit(2)
