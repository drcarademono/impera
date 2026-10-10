import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import sys
import tempfile
import unittest
from pathlib import Path
EDITORS=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(EDITORS))
from PyQt6 import QtWidgets
import u5_tlk_gui
import u5_dialogue_editor
from u5_formats import TAIL, tlk_header, tlk_segments, build_tlk
APP=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
GAME=Path(os.environ.get('U5_GAME_DIR',str(EDITORS.parent/'Ultima 5')))
TOOLS=[u5_tlk_gui,u5_dialogue_editor]

class DialogueTest(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name)

    def test_engine_word_table_matches_source(self):
        import re
        source=(EDITORS.parent/'src/vars.c').read_text()
        array=source.split('char* D_24ea[0x80] =')[1].split('};')[0]
        words=[]
        for line in array.splitlines():
            match=re.search(r'_STATIC_TEXT\([^,]+, "([^"]+)"\)',line)
            if match:words.append(match.group(1))
            elif line.strip()=='0,':words.append(None)
        for tool in TOOLS:self.assertEqual(tool.TOKEN_WORDS,words)

    def test_controls_quotes_labels_and_operands_roundtrip(self):
        examples=[b'\xa2\x0d\xa0\x38\xa2\x8d', b'\x85\xb0\xb0\xb3\xa1',
                  b'\x86\x91\x8c\xff\xfe\x91\x9f\xff', b'\x90\x9f\xc1',
                  b'\x91', b'\x90hello', b'\xc0\xff',b'\x1c\x08']
        for tool in TOOLS:
            for raw in examples:
                with self.subTest(tool=tool.__name__,raw=raw):
                    text=tool.decode_entry(raw,tool.count_label_occurrences([raw]))
                    self.assertEqual(tool.encode_entry(text)[:-1],raw)
                    self.assertEqual(tool.encode_entry(tool.pretty_decode_bytes(raw))[:-1],raw)

    def test_label_count_ignores_opcode_operands(self):
        for tool in TOOLS:
            self.assertEqual(tool.count_label_occurrences([b'\x86\x91\xfe\x92\x93\x8c\x94\x9f']),{15:1})
            self.assertEqual(tool.encode_entry('<Label 15>Hello'),b'\x90\x9f\xc8\xe5\xec\xec\xef\0')
            self.assertEqual(tool.is_label_start('<Any><Label 15>Hello'),15)

    def test_tails_and_unterminated_final_entry(self):
        for tool in TOOLS:
            for suffix in [TAIL,TAIL+b'\0\0']:
                blob=build_tlk([(1,0)],[b'name\0hello\0'+suffix])
                self.assertEqual(tool.extract_npc_entries(tlk_header(blob),blob),[(1,[b'name',b'hello'])])
            blob=build_tlk([(1,0)],[b'name\0last'])
            self.assertEqual(tool.extract_npc_entries(tlk_header(blob),blob)[0][1][-1],b'last')

    def test_noop_saves_preserve_padding(self):
        for tool in TOOLS:
            blob=build_tlk([(1,0)],[b'Name\0Desc\0Hi\0Job\0Bye\0'+TAIL+b'\0\0'])
            source=self.directory/'source.TLK';out=self.directory/'out.TLK';source.write_bytes(blob)
            model=tool.TLKModel();model.load(str(source));model.save_as(str(out))
            self.assertEqual(out.read_bytes(),blob)

    def test_edit_one_entry_and_reopen(self):
        for tool in TOOLS:
            source=self.directory/'source.TLK';out=self.directory/'out.TLK'
            tool.write_tlk(str(source),[(1,[b'Name',b'Desc',b'Hi',b'Job',b'Bye'])])
            model=tool.TLKModel();model.load(str(source));model.decoded[0][2]='"Hello"<New Line><Gold - 003>'
            model.save_as(str(out))
            loaded=tool.TLKModel();loaded.load(str(out))
            self.assertEqual(loaded.decoded[0][2],'"Hello"<New Line><Gold - 003>')
            self.assertEqual(len(loaded.entries_raw[0]),5)
            self.assertTrue(tlk_segments(*tool.read_tlk(str(out)))[0].endswith(TAIL))

    def test_invalid_bytes_unicode_and_size_leave_destination_untouched(self):
        for tool in TOOLS:
            for text in ['<Item: 999>','<Unknown: 0>','snowman \u2603']:
                with self.subTest(tool=tool.__name__,text=text),self.assertRaises(ValueError):tool.encode_entry(text)
            out=self.directory/'out.TLK';out.write_bytes(b'keep')
            with self.assertRaises(ValueError):tool.write_tlk(str(out),[(1,[b'x'*1024])])
            self.assertEqual(out.read_bytes(),b'keep')

    def test_invalid_gui_entry_does_not_crash_or_overwrite(self):
        from unittest.mock import patch
        for tool in TOOLS:
            window=tool.MainWindow()
            source=self.directory/'source.TLK'
            tool.write_tlk(str(source),[(1,[b'Name',b'Desc',b'Hi',b'Job',b'Bye'])])
            window.model.load(str(source))
            window._current_npc_idx=0;window._current_edit_idx=2
            before=window.model.decoded[0][2]
            window.editor.setPlainText('<Set Flag>')
            with patch.object(QtWidgets.QMessageBox,'warning'):
                self.assertFalse(window.apply_editor_if_dirty())
            self.assertEqual(window.model.decoded[0][2],before)
            window._editor_dirty=False;window.close()

    def test_changed_npc_preserves_other_segment_padding(self):
        for tool in TOOLS:
            source=self.directory/'source.TLK';out=self.directory/'out.TLK'
            second=b'Other\0Desc\0Hi\0Job\0Bye\0'+TAIL+b'\0\0'
            blob=build_tlk([(1,0),(2,0)],[b'Name\0Desc\0Hi\0Job\0Bye\0'+TAIL,second])
            source.write_bytes(blob)
            model=tool.TLKModel();model.load(str(source));model.decoded[0][2]='Hello'
            model.save_as(str(out))
            self.assertEqual(tlk_segments(*tool.read_tlk(str(out)))[1],second)

    @unittest.skipUnless((GAME/'CASTLE.TLK').exists(),'Optional original game resources unavailable')
    def test_original_dialogue_entries_and_gui_roundtrip(self):
        for tool in TOOLS:
            for source in GAME.glob('*.TLK'):
                with self.subTest(tool=tool.__name__,source=source.name):
                    headers,blob=tool.read_tlk(str(source))
                    for _,entries in tool.extract_npc_entries(headers,blob):
                        for entry in entries:
                            decoded=tool.decode_entry(entry,tool.count_label_occurrences([entry]))
                            self.assertEqual(tool.encode_entry(decoded)[:-1],entry)
                    window=tool.MainWindow();window.model.load(str(source));APP.processEvents()
                    out=self.directory/'out.TLK';window.model.save_as(str(out))
                    self.assertEqual(out.read_bytes(),blob)
                    window.close();window.deleteLater();APP.processEvents()

if __name__=='__main__':unittest.main()
