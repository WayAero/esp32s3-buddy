"""统一 Component Manager 生成的本地路径；不重新解析或升级依赖。"""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def normalize_text(content):
    # v3 锁格式 source.path 使用六空格缩进；其他字段和换行保持原样。
    return re.sub(r'^      path: [^\r\n]+',
                  lambda match: match.group().replace('\\', '/'),
                  content, flags=re.MULTILINE)


def main():
    names = subprocess.check_output(
        ['git', '-C', str(ROOT), 'ls-files', '-z', '--',
         'dependencies.lock', '**/dependencies.lock'],
    ).decode('utf-8').split('\0')
    for name in filter(None, names):
        path = ROOT / name
        original = path.read_bytes()
        normalized = normalize_text(original.decode('utf-8')).encode('utf-8')
        if original != normalized:
            path.write_bytes(normalized)
            print(f'Normalized local paths: {name}')


if __name__ == '__main__':
    main()
