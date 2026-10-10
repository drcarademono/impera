"""Run Qt bindings in separate processes to avoid loading two Qt runtimes."""
import os
import subprocess
import sys
from pathlib import Path

tests=Path(__file__).resolve().parent
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
for name in ['test_formats.py','test_pyside.py','test_pyqt.py']:
    result=subprocess.run([sys.executable,'-m','unittest','discover','-s',str(tests),'-p',name,'-v'])
    if result.returncode:
        raise SystemExit(result.returncode)
