import datetime as dt
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import urllib.error
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import holiday_sync
import chinese_calendar


def sample(year=2026):
    return {'year':year,'days':[{'date':f'{year}-10-01','name':'国庆节','isOffDay':True}]}


class HolidaySyncTests(unittest.TestCase):
    def test_validation(self):
        for data in ({'year':2025,'days':[]},{'year':2026,'days':[]},
                     {'year':2026,'days':[{'date':'2026-10-01','name':'节日','isOffDay':'true'}]},
                     {'year':2026,'days':sample()['days']*2}):
            with self.assertRaises((ValueError,TypeError)):holiday_sync.validate(data,2026)

    def test_atomic_cache_and_failure_preserves_previous(self):
        with tempfile.TemporaryDirectory() as tmp:
            result=holiday_sync.synchronize([2026],tmp,lambda y:sample(y))
            self.assertEqual(result['updated'],[2026]);path=Path(tmp)/'2026.json';before=path.read_bytes()
            def unavailable(y): raise OSError('offline')
            self.assertEqual(holiday_sync.synchronize([2026],tmp,unavailable)['failed'],[2026])
            self.assertEqual(path.read_bytes(),before)
            def missing(y):raise urllib.error.HTTPError('https://example.invalid',404,'not published',{},None)
            self.assertEqual(holiday_sync.synchronize([2027],tmp,missing)['unpublished'],[2027])
            self.assertEqual(holiday_sync.synchronize([2027],tmp,lambda y:{'year':y,'days':[],'papers':[]})['unpublished'],[2027])
            self.assertFalse((Path(tmp)/'2027.json').exists())
            self.assertEqual(list(Path(tmp).iterdir()),[path])

    def test_cached_year_replaces_old_records_and_keeps_lunar_offline(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.dict(os.environ,{'XDG_DATA_HOME':tmp}):
            data=sample();data['days'][0]['isOffDay']=False
            holiday_sync.synchronize([2026],fetcher=lambda y:data)
            dates=chinese_calendar.metadata(dt.date(2026,10,1),dt.date(2026,10,2))
            self.assertFalse(dates['2026-10-01']['off'])
            self.assertNotIn('off',dates['2026-10-02'])
            self.assertTrue(dates['2026-10-01']['lunar'])
            path=holiday_sync.cache_directory()/'2026.json';path.write_text('{}')
            dates=chinese_calendar.metadata(dt.date(2026,10,1),dt.date(2026,10,1))
            self.assertTrue(dates['2026-10-01']['off'])

if __name__=='__main__':unittest.main()
