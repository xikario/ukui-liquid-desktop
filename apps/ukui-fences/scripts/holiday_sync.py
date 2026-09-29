#!/usr/bin/env python3
"""Explicit user-triggered update of holiday-cn yearly data; no schedule upload."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import tempfile
import urllib.error
import urllib.request

BASE = 'https://raw.githubusercontent.com/NateScarlet/holiday-cn/master/'
LIMIT = 512 * 1024


class UnpublishedYear(ValueError):
    pass


def cache_directory():
    return Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'ukui-fences/holidays'


def validate(data, year):
    if not isinstance(data, dict) or type(data.get('year')) is not int or data['year'] != year:
        raise ValueError('年份不匹配')
    days = data.get('days')
    if days == [] and data.get('papers', []) == []:
        raise UnpublishedYear('调休安排尚未发布')
    if not isinstance(days, list) or not 1 <= len(days) <= 366:
        raise ValueError('节假日列表无效')
    seen = set()
    for item in days:
        if not isinstance(item, dict): raise ValueError('日期格式无效')
        date = dt.date.fromisoformat(item.get('date', ''))
        if date.year != year or str(date) in seen: raise ValueError('日期年份或重复记录错误')
        if type(item.get('isOffDay')) is not bool: raise ValueError('休班标记无效')
        if not isinstance(item.get('name'), str) or not 1 <= len(item['name']) <= 80:
            raise ValueError('节日名称无效')
        seen.add(str(date))
    return data


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('数据源发生重定向，请稍后重试')


def fetch(year):
    request = urllib.request.Request(BASE + str(year) + '.json', headers={'User-Agent': 'ukui-fences-calendar/1'})
    with urllib.request.build_opener(NoRedirect()).open(request, timeout=8) as response:
        content = response.read(LIMIT + 1)
    if len(content) > LIMIT: raise ValueError('数据文件过大')
    return validate(json.loads(content), year)


def synchronize(years, directory=None, fetcher=fetch):
    directory = Path(directory) if directory is not None else cache_directory()
    updated, unpublished, failed = [], [], []
    for year in sorted(set(years)):
        if not 1900 <= year <= 2101: raise ValueError('年份超出范围')
        try:
            data = validate(fetcher(year), year)
            directory.mkdir(parents=True, exist_ok=True)
            name = None
            try:
                with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=directory, delete=False) as out:
                    name = out.name
                    json.dump(data, out, ensure_ascii=False)
                    out.flush(); os.fsync(out.fileno())
                os.replace(name, directory / (str(year) + '.json'))
            finally:
                if name and os.path.exists(name): os.unlink(name)
            updated.append(year)
        except UnpublishedYear:
            unpublished.append(year)
        except urllib.error.HTTPError as error:
            if error.code == 404: unpublished.append(year)
            else: failed.append(year)
        except (OSError, ValueError, TypeError):
            failed.append(year)
    parts = []
    if updated: parts.append('已更新节假日：' + '、'.join(map(str, updated)))
    if unpublished: parts.append('数据源尚未发布：' + '、'.join(map(str, unpublished)))
    if failed: parts.append('同步失败，保留原数据：' + '、'.join(map(str, failed)))
    return {'updated': updated, 'unpublished': unpublished, 'failed': failed, 'message': '；'.join(parts)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--year', type=int, action='append', required=True)
    args = parser.parse_args()
    result = synchronize(args.year)
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(1 if result['failed'] else 0)
