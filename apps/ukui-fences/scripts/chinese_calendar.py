"""Offline Chinese calendar via the system ICU astronomical calendar.

ICU uses the China time zone explicitly, independent of the process locale/TZ.
Holiday overrides are official published dates, never inferred from weekends.
"""
import ctypes as c
import ctypes.util
import datetime as dt
import json
from pathlib import Path
import re

MONTHS = ('正月','二月','三月','四月','五月','六月','七月','八月','九月','十月','冬月','腊月')
DAYS = tuple('初'+x for x in '一二三四五六七八九十') + tuple('十'+x for x in '一二三四五六七八九') + ('二十',) + tuple('廿'+x for x in '一二三四五六七八九') + ('三十',)
FESTIVALS = {(1,1):'春节',(1,15):'元宵',(5,5):'端午',(7,7):'七夕',(8,15):'中秋',(9,9):'重阳',(12,8):'腊八'}

class LunarCalendar:
    def __init__(self):
        name = ctypes.util.find_library('icui18n')
        if not name: raise RuntimeError('系统缺少 ICU 农历支持')
        self.lib = c.CDLL(name)
        match = re.search(r'\.so\.(\d+)', name)
        suffix = '_'+match[1] if match else ''
        def bind(name, args, result):
            f = getattr(self.lib, name+suffix)
            f.argtypes, f.restype = args, result
            return f
        ptr = c.POINTER(c.c_int)
        opening = bind('ucal_open',[c.c_void_p,c.c_int32,c.c_char_p,c.c_int,ptr],c.c_void_p)
        self.set_ms = bind('ucal_setMillis',[c.c_void_p,c.c_double,ptr],None)
        self.get = bind('ucal_get',[c.c_void_p,c.c_int,ptr],c.c_int32)
        self.close = bind('ucal_close',[c.c_void_p],None)
        zone = c.create_string_buffer('Asia/Shanghai'.encode('utf-16-le'))
        error = c.c_int(0)
        self.calendar = opening(zone,13,b'zh_CN@calendar=chinese',0,c.byref(error))
        if error.value>0 or not self.calendar: raise RuntimeError('农历初始化失败')

    def date(self, date):
        error = c.c_int(0)
        noon = dt.datetime.combine(date,dt.time(12),dt.timezone(dt.timedelta(hours=8)))
        self.set_ms(self.calendar,noon.timestamp()*1000,c.byref(error))
        year, month, day, leap = [self.get(self.calendar,f,c.byref(error)) for f in (1,2,5,22)]
        if error.value>0: raise RuntimeError('农历日期转换失败')
        month += 1
        full_month = ('闰' if leap else '')+MONTHS[month-1]
        year_name = '甲乙丙丁戊己庚辛壬癸'[(year-1)%10]+'子丑寅卯辰巳午未申酉戌亥'[(year-1)%12]+'年'
        festival = '' if leap else FESTIVALS.get((month,day),'')
        # New Year's Eve is the last day of month 12 (29 or 30 days).
        if month==12 and day>=29:
            tomorrow = self.date(date+dt.timedelta(days=1))
            if tomorrow['month']==1 and tomorrow['day']==1: festival='除夕'
        festival = {(1,1):'元旦',(5,1):'劳动节',(10,1):'国庆节'}.get((date.month,date.day),festival)
        # Exact Qingming date for the bundled official year; no forecasts.
        if date==dt.date(2026,4,5): festival='清明'
        return {'month':month,'day':day,'leap':bool(leap),'lunar':full_month+DAYS[day-1],
                'yearName':year_name,'label':festival or (full_month if day==1 else DAYS[day-1]),'festival':festival}

    def __enter__(self): return self
    def __exit__(self,*args): self.close(self.calendar)


def metadata(begin,end):
    overrides={};known=set()
    system=Path('/usr/share/ukui-panel/plugin-calendar/html/jiejiari.json')
    if system.exists():
        try:
            for year,days in json.loads(system.read_text()).items():
                year=int(year.rsplit('y',1)[-1]);known.add(year)
                for key,value in days.items():
                    overrides[f'{year}-{key[1:3]}-{key[3:5]}']={'off':str(value)=='2','holiday':''}
        except (OSError,ValueError,TypeError): pass
    data=json.loads(Path(__file__).with_name('china_holidays_2026.json').read_text())
    known.add(data['year'])
    for item in data['days']:overrides[item['date']]={'off':item['isOffDay'],'holiday':item['name']}
    result={}
    with LunarCalendar() as calendar:
        cursor=begin
        while cursor<=end:
            value=calendar.date(cursor);value['scheduleKnown']=cursor.year in known
            value.update(overrides.get(str(cursor),{}));result[str(cursor)]=value
            cursor+=dt.timedelta(days=1)
    return result
