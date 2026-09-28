"""Login helper decisions without disturbing the real desktop."""
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch, Mock

spec=importlib.util.spec_from_file_location('session_start',Path(__file__).resolve().parents[1]/'scripts/session-start.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LoginTest(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        home=Path(self.directory.name)
        for path in ['.local/bin/ukui-panel-liquid','.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so']:
            file=home/path;file.parent.mkdir(parents=True,exist_ok=True);file.touch()
        self.home=patch.object(module.Path,'home',return_value=home)
        self.home.start();self.addCleanup(self.home.stop)

    def test_already_loaded_is_noop(self):
        with patch.object(module,'panels',return_value={123:True}),patch.object(module.time,'sleep'),patch.object(module.subprocess,'run') as run,patch.object(module.subprocess,'Popen') as spawn:
            module.ensure_panel(io.StringIO())
            run.assert_not_called();spawn.assert_not_called()

    def test_native_panel_is_stopped_before_launch(self):
        state={123:False}
        def stop(*args,**kwargs):
            self.assertEqual(args[0][-1],'ukui-panel.desktop');state.clear()
        def spawn(*args,**kwargs):
            self.assertFalse(state);state[456]=True;return Mock(pid=456)
        with patch.object(module,'panels',side_effect=lambda:state.copy()),patch.object(module.time,'sleep'),patch.object(module.subprocess,'run',side_effect=stop),patch.object(module.subprocess,'Popen',side_effect=spawn):
            module.ensure_panel(io.StringIO())
            self.assertEqual(state,{456:True})

    def test_missing_plugin_leaves_original(self):
        (module.Path.home()/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so').unlink()
        with patch.object(module.subprocess,'run') as run,patch.object(module.subprocess,'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError,'missing'):module.ensure_panel(io.StringIO())
            run.assert_not_called();spawn.assert_not_called()

    def test_stop_failure_does_not_launch_duplicate(self):
        with patch.object(module,'panels',return_value={123:False}),patch.object(module.time,'sleep'),patch.object(module,'wait_for',side_effect=[True,False]),patch.object(module.subprocess,'run'),patch.object(module.subprocess,'Popen') as spawn:
            with self.assertRaisesRegex(RuntimeError,'did not stop'):module.ensure_panel(io.StringIO())
            spawn.assert_not_called()


if __name__=='__main__':unittest.main()
