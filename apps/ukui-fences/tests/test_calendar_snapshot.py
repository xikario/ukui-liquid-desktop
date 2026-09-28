import importlib.util
import sys
from pathlib import Path
import sqlite3
import tempfile
import datetime as dt
import unittest
sys.path.insert(0,str(Path(__file__).parents[1]/'scripts'))
spec=importlib.util.spec_from_file_location('reader',Path(__file__).parents[1]/'scripts/calendar_snapshot.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CalendarTests(unittest.TestCase):
    def test_native_dates_spans_and_repeats_are_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'calendar.db'
            with sqlite3.connect(db) as c:
                c.execute('CREATE TABLE Schedule(id, start_date, end_date, start_hour, start_minute, descript, repeat, isAllDay, isLunar, beginrepeat, endrepeat)')
                c.executemany('INSERT INTO Schedule VALUES(?,?,?,?,?,?,?,?,?,?,?)',[
                    ('a','2026-09-28','2026-09-28','17','30','待办','不重复',0,0,'',''),
                    ('b','2026-09-27','2026-09-29','0','0','跨天','不重复',1,0,'',''),
                    ('c','2026-09-01','2026-09-01','9','0','每日','每天',0,0,'无限重复',''),
                    ('d','2026-09-01','2026-09-01','9','0','未知','自定义',0,0,'',''),
                ])
            original=db.read_bytes();r=m.read_snapshot(db,dt.date(2026,9,28),dt.date(2026,9,30))
            self.assertEqual(len(r['items']),5);self.assertEqual(db.read_bytes(),original)
            self.assertTrue(r['warning']);self.assertEqual(next(i for i in r['items'] if i['title']=='待办')['time'],'17:30')
            self.assertEqual(next(i for i in r['items'] if i['title']=='跨天')['time'],'全天')
    def test_missing_database_is_not_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            db=Path(tmp)/'absent.db';r=m.read_snapshot(db,dt.date.today(),dt.date.today())
            self.assertEqual(r['items'],[]);self.assertFalse(db.exists())
class ChineseCalendarTests(unittest.TestCase):
    def test_lunar_festivals_leap_month_and_year_boundary(self):
        from chinese_calendar import LunarCalendar
        with LunarCalendar() as calendar:
            for solar,lunar,festival in [
                ('2026-02-17','正月初一','春节'),('2026-02-16','腊月廿九','除夕'),
                ('2026-06-19','五月初五','端午'),('2026-09-25','八月十五','中秋'),
                ('2025-07-25','闰六月初一',''),('2026-04-05','二月十八','清明'),
                ('2026-10-01','八月廿一','国庆节')]:
                value=calendar.date(dt.date.fromisoformat(solar))
                self.assertEqual(value['lunar'],lunar,solar)
                self.assertEqual(value['festival'],festival,solar)
            self.assertTrue(calendar.date(dt.date(2025,7,25))['leap'])
            self.assertEqual(calendar.date(dt.date(2026,2,17))['yearName'],'丙午年')

    def test_official_2026_schedule_and_unknown_year(self):
        from chinese_calendar import metadata
        days=metadata(dt.date(2026,1,1),dt.date(2026,12,31))
        # Independently transcribed from State Council notice 2025 No. 7.
        expected_off=set()
        for month,first,last in [(1,1,3),(2,15,23),(4,4,6),(5,1,5),(6,19,21),(9,25,27),(10,1,7)]:
            expected_off.update(str(dt.date(2026,month,day)) for day in range(first,last+1))
        expected_work={'2026-01-04','2026-02-14','2026-02-28','2026-05-09','2026-09-20','2026-10-10'}
        self.assertEqual({day for day,v in days.items() if v.get('off') is True},expected_off)
        self.assertEqual({day for day,v in days.items() if v.get('off') is False},expected_work)
        self.assertNotIn('off',days['2026-09-28'])
        future=metadata(dt.date(2030,1,1),dt.date(2030,1,1))['2030-01-01']
        self.assertFalse(future['scheduleKnown']);self.assertNotIn('off',future)

if __name__=='__main__':unittest.main()
