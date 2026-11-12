#!/usr/bin/env python3
"""곡의 Music IR 을 조회하고 검사하고, 고칠 범위를 정해 고친 결과를 검사한다.

조회는 큰 데서 작은 데로: 곡 전체(overview), 구간(section), 트랙과 의존(tracks, deps), 실제 음표와 코드 자리(notes, source).
고치기는 plan, 생성기 수정, verify, apply. 자세한 것은 docs/plans/music-ir.md.

사용법:
  tools/music-ir.py overview glass-circuit
  tools/music-ir.py section rusted-wings "마지막 코러스"
  tools/music-ir.py deps rusted-wings --section final --track family:strings
  tools/music-ir.py source rusted-wings --section final --track vln
  tools/music-ir.py plan rusted-wings --sections final --tracks family:strings --protect role:main-melody,piano,harmony
  tools/music-ir.py verify rusted-wings
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from musicir.cli import main  # noqa: E402

sys.exit(main())
