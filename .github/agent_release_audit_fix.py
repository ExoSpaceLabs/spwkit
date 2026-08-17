from pathlib import Path

p = Path('tests/api_c_compile.c')
text = p.read_text()
old = 'static spw_result_t (*stats_fn)(spw_port_t*, spw_statistics_t*) = spw_port_get_statistics;'
new = 'static spw_result_t (*stats_fn)(const spw_port_t*, spw_statistics_t*) = spw_port_get_statistics;'
if old not in text:
    raise SystemExit('release audit stats function-pointer marker not found')
p.write_text(text.replace(old, new, 1))
print('release audit compile-probe fix applied')
