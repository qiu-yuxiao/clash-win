#!/usr/bin/env python3
# 全量回归运行器（PowerShell run_tests_v4.ps1 的 Python 等价实现）。
# 关键：360 对 build\bin\Release 路径下进程的写限制会导致 live probe 用例失败，
# 因此把测试 exe + sidecar/locales/assets 复制到项目内隔离目录 build/cwtest 运行，
# 并把 TMP/TEMP 重定向到该目录，避免 %TEMP% 下 exe 被安全软件清除。
import os
import sys
import shutil
import subprocess

REPO = r"C:\Users\sun_y\Documents\AntiGravity_Projects\ClashWin"
REL = os.path.join(REPO, "build", "bin", "Release")
RUNDIR = os.path.join(REPO, "build", "cwtest")
TMPDIR = os.path.join(RUNDIR, "tmp")
LOGROOT = os.path.join(REPO, "build", "cwtest_logs")
EXE = os.path.join(RUNDIR, "clashwin_test.exe")


def copy_tree_if_newer(src, dst):
    if not os.path.exists(src):
        return
    os.makedirs(dst, exist_ok=True)
    for name in os.listdir(src):
        s = os.path.join(src, name)
        d = os.path.join(dst, name)
        if os.path.isdir(s):
            copy_tree_if_newer(s, d)
        else:
            # 仅当目标缺失或更新时复制，省去重复 47MB 拷贝
            if not os.path.exists(d) or os.path.getmtime(s) > os.path.getmtime(d):
                shutil.copy2(s, d)


def setup():
    os.makedirs(RUNDIR, exist_ok=True)
    os.makedirs(TMPDIR, exist_ok=True)
    os.makedirs(LOGROOT, exist_ok=True)
    # 测试 exe
    shutil.copy2(os.path.join(REL, "clashwin_test.exe"), EXE)
    # sidecar（live probe 需要 test core）
    copy_tree_if_newer(os.path.join(REPO, "sidecar"), os.path.join(RUNDIR, "sidecar"))
    # locales
    copy_tree_if_newer(os.path.join(REL, "locales"), os.path.join(RUNDIR, "locales"))
    # assets/locales（t25_i18n 读取仓库 assets/locales/layout.json）
    os.makedirs(os.path.join(RUNDIR, "assets"), exist_ok=True)
    copy_tree_if_newer(os.path.join(REPO, "assets", "locales"),
                       os.path.join(RUNDIR, "assets", "locales"))
    # t24 updater 诊断可能需要 resources
    copy_tree_if_newer(os.path.join(REL, "resources"), os.path.join(RUNDIR, "resources"))


def main():
    setup()
    filter_args = sys.argv[1:]
    log_path = os.path.join(LOGROOT, "tests_full.log")
    env = os.environ.copy()
    env["TMP"] = TMPDIR
    env["TEMP"] = TMPDIR
    # 避免任何代理干扰（与构建一致）
    for k in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY"):
        env.pop(k, None)
    with open(log_path, "w", encoding="utf-8") as f:
        proc = subprocess.run([EXE, *filter_args], cwd=RUNDIR, env=env,
                              stdout=f, stderr=subprocess.STDOUT)
    # 打印尾部摘要
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    tail = lines[-40:]
    print("\n".join(tail))
    print(f"\n[runner] EXIT={proc.returncode}  log={log_path}")
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
