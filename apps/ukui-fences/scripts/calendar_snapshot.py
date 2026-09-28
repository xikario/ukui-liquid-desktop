#!/usr/bin/python3
"""Read the native Kylin calendar, without changing schedules or sending reminders."""
import argparse
import datetime as dt
import json
from pathlib import Path
import sqlite3
from chinese_calendar import metadata


def day(value):
    return dt.date.fromisoformat(str(value)[:10])


def read_snapshot(path, begin, end):
    path=Path(path)
    if not path.exists():
        return {'items': [], 'warning': '系统日历尚无数据'}
    items=[];warnings=set()
    with sqlite3.connect(path.resolve().as_uri()+'?mode=ro', uri=True, timeout=.3) as conn:
        conn.execute('PRAGMA query_only=ON');conn.row_factory=sqlite3.Row
        for row in conn.execute('SELECT * FROM Schedule LIMIT 10000'):
            r=dict(row)
            try:
                first=day(r['start_date']);last=max(first,day(r['end_date']))
                rule=str(r.get('repeat','不重复'))
                repeating=rule not in ('','不重复','不重複','No repeat','Never')
                if repeating and (r.get('isLunar') or rule not in ('每天','每日','每周','每星期','每月','每年','工作日','每个工作日')):
                    warnings.add('部分重复事项请在系统日历查看')
                    repeating=False
                until=end
                if r.get('beginrepeat') not in ('无限重复',''):
                    try:until=min(end,day(r.get('endrepeat',str(end))))
                    except ValueError:pass
                duration=(last-first).days
                starts=[]
                if not repeating:
                    if last>=begin and first<=end:starts=[first]
                else:
                    cursor=max(first,begin-dt.timedelta(days=min(duration,366)))
                    while cursor<=until:
                        yes=(rule in ('每天','每日') or
                             rule in ('每周','每星期') and cursor.weekday()==first.weekday() or
                             rule=='每月' and cursor.day==first.day or
                             rule=='每年' and (cursor.month,cursor.day)==(first.month,first.day) or
                             rule in ('工作日','每个工作日') and cursor.weekday()<5)
                        if yes:starts.append(cursor)
                        cursor+=dt.timedelta(days=1)
                for date in starts:
                    all_day=bool(r.get('isAllDay'))
                    hour=int(r['start_hour']);minute=int(r['start_minute'])
                    clock=dt.time(hour,minute).strftime('%H:%M')
                    items.append({'id':str(r['id'])+'@'+str(date),'date':str(date),'endDate':str(date+dt.timedelta(days=duration)),
                                  'time':'全天' if all_day else clock,'title':str(r['descript']), 'allDay':all_day})
            except (KeyError,ValueError,TypeError,OverflowError):
                warnings.add('部分日程格式异常，请在系统日历查看')
    items.sort(key=lambda x:(x['date'],not x['allDay'],x['time'],x['id']))
    return {'items':items,'warning':'；'.join(sorted(warnings))}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--database',required=True);parser.add_argument('--begin',required=True);parser.add_argument('--end',required=True)
    args=parser.parse_args()
    try:
        begin,end=day(args.begin),day(args.end)
        if end<begin or (end-begin).days>400:raise ValueError('Invalid date range')
        snapshot=read_snapshot(args.database,begin,end)
        try:snapshot['dates']=metadata(begin,end)
        except (OSError,RuntimeError,ValueError,AttributeError) as error:
            snapshot['dates']={};snapshot['calendarWarning']='农历数据暂不可用'
        print(json.dumps(snapshot,ensure_ascii=False))
    except (sqlite3.Error,ValueError,OSError) as error:
        print(json.dumps({'error':'系统日历暂时无法读取，请稍后重试'},ensure_ascii=False));raise SystemExit(1)
