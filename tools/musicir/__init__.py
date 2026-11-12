"""Music IR: 곡의 구간, 화성, 트랙의 역할, 주제, 의존과 음표를 만든 코드 자리를 적는 곁 파일 (<곡>.ir.json).

작곡 생성기는 start, trace, save 를 쓰고, 에이전트와 사람은 tools/music-ir.py 로 조회하고 검사한다.
"""
from .record import Song, save, start, trace  # noqa: F401
