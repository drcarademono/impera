import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path
EDITORS = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(EDITORS))
from PySide6 import QtWidgets, QtGui, QtCore
from u5_formats import lzw_encode, lzw_decode
import u5_16_editor as images
import u5_tiles as tiles
import u5_map_editor as maps
import u5_brit_map as brit
import u5_init_editor as state
import u5_story_tool as story
APP = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
GAME = Path(os.environ.get('U5_GAME_DIR',str(EDITORS.parent/'Ultima 5')))

class EditorTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.tile_path = self.directory/'TILES.16'
        self.tile_path.write_bytes(struct.pack('<I',65536)+lzw_encode(bytes(65536)))

    def fixture_image(self, width=9, height=2):
        # Independent native layout: paired offsets, 8-pixel padded color rows,
        # and individually byte-padded mask rows. Mask is intentionally separate.
        stride = ((width+7)//8)*4
        color = struct.pack('<HH',width,height)+b'\xff'*(stride*height)
        mask = struct.pack('<HH',width,height)+b'\x80\x80\x40\0'
        blob = struct.pack('<HHH',1,6,6+len(color)+7)+color+b'padding'+mask
        path = self.directory/'ITEMS.16'
        path.write_bytes(struct.pack('<I',len(blob))+lzw_encode(blob))
        return path, blob

    def test_image_mask_offset_and_row_padding(self):
        path,blob = self.fixture_image()
        file = images.read_any16(path)
        self.assertEqual(file.count,1)
        img = file.slots[0].qimage
        self.assertEqual([img.pixelColor(x,y).alpha() for x,y in [(0,0),(8,0),(1,1),(0,1)]], [0,0,0,255])
        self.assertEqual(images.build_multi_blob(file),blob)

    def test_changed_alpha_regenerates_mask(self):
        path,_ = self.fixture_image()
        file = images.read_any16(path)
        file.slots[0].qimage.setPixelColor(0,0,QtGui.QColor(255,255,255,255))
        file.slots[0].qimage.setPixelColor(5,1,QtGui.QColor(1,2,3,0))
        blob = images.build_multi_blob(file)
        path.write_bytes(struct.pack('<I',len(blob))+lzw_encode(blob))
        reread = images.read_any16(path).slots[0].qimage
        self.assertEqual(reread.pixelColor(0,0).alpha(),255)
        self.assertEqual(reread.pixelColor(5,1).alpha(),0)
        self.assertEqual(reread.pixelColor(1,1).alpha(),0)

    def test_import_transparent_png_and_reject_partial_alpha(self):
        path,_ = self.fixture_image()
        file = images.read_any16(path)
        imported = file.slots[0].qimage.copy()
        imported.setPixelColor(3,0,QtGui.QColor(7,8,9,0))
        imported.save(str(self.directory/'img_000.png'))
        file.slots = images.import_folder_multi(file,self.directory)
        images.build_multi_blob(file)
        file.slots[0].qimage.setPixelColor(3,0,QtGui.QColor(255,255,255,100))
        with self.assertRaises(ValueError): images.build_multi_blob(file)

    def test_bad_image_offsets_and_overflow(self):
        path,_ = self.fixture_image()
        invalid = struct.pack('<HHH',1,2,0)+bytes(20)
        path.write_bytes(struct.pack('<I',len(invalid))+lzw_encode(invalid))
        with self.assertRaises(ValueError): images.read_any16(path)
        img = QtGui.QImage(512,256,QtGui.QImage.Format.Format_ARGB32);img.fill(QtGui.QColor(255,255,255))
        file = images.U5File(None,'multi',2,True,[images.ImageSlot(512,256,img),images.ImageSlot(512,256,img)])
        with self.assertRaises(ValueError):images.build_multi_blob(file)

    def test_all_tiles_readers_handle_raw_and_compressed(self):
        raw = random.Random(0).randbytes(65536)
        for data in [raw,struct.pack('<I',65536)+lzw_encode(raw)]:
            self.tile_path.write_bytes(data)
            self.assertEqual(tiles.read_tiles16_blob(self.tile_path),raw)
            self.assertEqual(images.build_tiles_blob(images.read_any16(self.tile_path)),raw)
            self.assertEqual(len(maps.read_tiles16(self.tile_path)),512)
            self.assertEqual(brit.read_lzw_blob_auto(self.tile_path,65536),raw)

    def test_tiles_cannot_silently_discard_alpha(self):
        file = images.read_any16(self.tile_path)
        file.slots[0].qimage.setPixelColor(0,0,QtGui.QColor(0,0,0,0))
        with self.assertRaises(ValueError): images.build_tiles_blob(file)
        with self.assertRaises(ValueError): tiles.images_to_blob([s.qimage for s in file.slots])

    def world_fixture(self, mapping):
        overlay = bytearray(b'\xab'*16000)
        overlay[0x3886:0x3986] = bytes(mapping)
        data_path, map_path = self.directory/'DATA.OVL', self.directory/'BRIT.DAT'
        data_path.write_bytes(overlay);map_path.write_bytes(bytes([2])*256)
        return maps.U5World.load(data_path,map_path,self.tile_path), bytes(overlay)

    def test_britannia_cli_exports(self):
        import subprocess
        from PIL import Image
        world,_=self.world_fixture([0]+[255]*255)
        quick=self.directory/'quick.png';ids=self.directory/'ids.png'
        subprocess.run([sys.executable,str(EDITORS/'u5_brit_map.py'),
            '--data-ovl',str(world.data_ovl),'--brit',str(world.brit_dat),
            '--out',str(quick),'--dump-ids',str(ids),'--scale','1'],check=True,capture_output=True)
        with Image.open(ids) as image:
            self.assertEqual(image.size,(256,256))
            self.assertEqual(image.getpixel((0,0)),2)
            self.assertEqual(image.getpixel((16,0)),1)
        with Image.open(quick) as image:self.assertEqual(image.size,(256,256))

    def test_npc_basement_overlay(self):
        path=self.directory/'TOWNE.DAT';path.write_bytes(bytes(16384))
        data=bytearray(4608);base=3*576
        data[base+16+3:base+16+6]=bytes([10]*3)
        data[base+16+6:base+16+9]=bytes([11]*3)
        data[base+16+9:base+16+12]=bytes([255]*3)
        data[base+512+1]=0xc4
        (self.directory/'towne.npc').write_bytes(data)
        backend=maps.SettlementBackend(path,self.tile_path);backend.select_map(3)
        canvas=maps.MapCanvas(backend);canvas.set_npc_mode(True)
        self.assertEqual([npc for npc,_ in canvas._npc_rects],[1])
        backend.select_level(1);canvas.redraw_entire_map()
        self.assertEqual(canvas._npc_rects,[])

    def test_brit_water_edit_updates_mapping(self):
        world, original = self.world_fixture([0]+[255]*255)
        world.paint_tile(16,0,9);world.save_brit_dat()
        loaded = maps.U5World.load(world.data_ovl,world.brit_dat,self.tile_path)
        self.assertEqual(loaded.tile_at(16,0),9)
        self.assertEqual(loaded.tile_at(17,0),1)
        overlay = world.data_ovl.read_bytes()
        self.assertEqual(overlay[:0x3886],original[:0x3886])
        self.assertEqual(overlay[0x3986:],original[0x3986:])

    def test_brit_shared_chunk_edits_do_not_modify_other_locations(self):
        world,_ = self.world_fixture([0,0]+[255]*254)
        world.paint_tile(0,0,7);world.save_brit_dat()
        loaded=maps.U5World.load(world.data_ovl,world.brit_dat,self.tile_path)
        self.assertEqual(loaded.tile_at(0,0),7)
        self.assertEqual(loaded.tile_at(16,0),2)

    def test_brit_noop_preserves_originals(self):
        world, original = self.world_fixture([0,0]+[255]*254)
        world.save_brit_dat()
        self.assertEqual(world.data_ovl.read_bytes(),original)
        self.assertEqual(world.brit_dat.read_bytes(),bytes([2])*256)

    def test_brit_unrepresentable_map_does_not_write(self):
        world,original=self.world_fixture([255]*256)
        for i in range(256): world.paint_tile((i%16)*16,(i//16)*16, i)
        # One corner equals water; give every block another non-water pixel.
        for i in range(256): world.paint_tile((i%16)*16+1,(i//16)*16, 2)
        with self.assertRaises(ValueError):world.save_brit_dat()
        self.assertEqual(world.data_ovl.read_bytes(),original)
        self.assertEqual(world.brit_dat.read_bytes(),bytes([2])*256)

    def test_settlement_tail_and_basements(self):
        for name,basement_map in [('CASTLE',0),('TOWNE',3),('KEEP',7)]:
            path=self.directory/(name+'.DAT')
            data=bytes(16384)+b'keep extension bytes';path.write_bytes(data)
            backend=maps.SettlementBackend(path,self.tile_path);backend.select_map(basement_map)
            self.assertEqual(backend.level_names[0],'Basement')
            backend.save()
            self.assertEqual(path.read_bytes(),data)

    def test_npc_schedule_roundtrip_and_tslot3(self):
        path=self.directory/'TOWNE.NPC';data=random.Random(1).randbytes(4608);path.write_bytes(data)
        npc=maps.NPCManager(path)
        npc.save();self.assertEqual(path.read_bytes(),data)
        npc.set_xy_for_tslot(0,1,3,12,13)
        self.assertEqual(npc.get_xyz_for_tslot(0,1,1)[:2],(12,13))
        self.assertEqual(maps.tslot_to_loc_index(2),2)

    def test_cbt_party_directions_preserve_special_bytes(self):
        path=self.directory/'BRIT.CBT';data=random.Random(2).randbytes(352);path.write_bytes(data)
        backend=maps.CBTBackend(path,self.tile_path)
        backend.paint_tile(5,6,99);backend.save()
        expected=bytearray(data);expected[6*32+5]=99
        self.assertEqual(path.read_bytes(),bytes(expected))
        self.assertEqual(backend._dir_to_row, {'E':1,'W':2,'S':3,'N':4})
        path.write_bytes(b'')
        with self.assertRaises(ValueError):maps.CBTBackend(path,self.tile_path)

    def test_combat_coordinate_widget_preserves_sentinels(self):
        widget=maps._PairsEditor(16,8)
        widget.set_pairs([255]*16,[255]*16)
        widget.sp_x[0].setValue(3)
        self.assertEqual(widget.pairs(),([3]+[255]*15,[255]*16))
        layout=widget.layout()
        positions=[layout.getItemPosition(i)[:2] for i in range(layout.count())]
        self.assertEqual(len(positions),len(set(positions)))

    def test_underworld_roundtrip_preserves_extension(self):
        path=self.directory/'UNDER.DAT'
        original=random.Random(17).randbytes(65536)+b'x'*256
        path.write_bytes(original)
        backend=maps.UnderBackend(path,self.tile_path);backend.save()
        self.assertEqual(path.read_bytes(),original)

    def test_init_refresh_is_read_only(self):
        model=state.InitGamModel()
        # Unknown/out-of-range UI values must survive opening and refresh.
        model.bytes[:]=random.Random(3).randbytes(4192)
        before=bytes(model.bytes)
        tabs=[cls(model) for cls in [state.PartyTab,state.TimeTab,state.InventoryTab,state.MoonstoneTab,state.FlagsTab,state.RawBytesTab,state.HexEditorTab]]
        self.assertEqual(bytes(model.bytes),before)
        model.data_changed.emit()
        self.assertEqual(bytes(model.bytes),before)

    def test_init_single_control_does_not_reset_others(self):
        model=state.InitGamModel();model.bytes[0x2b5]=3
        tab=state.PartyTab(model);before=bytes(model.bytes)
        tab.x_spin._spin.setValue(123)
        expected=bytearray(before);expected[0x2f0]=123
        self.assertEqual(model.bytes,expected)

    def test_init_save_as_and_invalid_input(self):
        model=state.InitGamModel();a,b=self.directory/'a.GAM',self.directory/'b.GAM'
        model.save(a);model.save(b);model.set_u8(0,42);model.save()
        self.assertEqual(a.read_bytes()[0],0);self.assertEqual(b.read_bytes()[0],42)
        self.assertEqual(model.u16(-1),0)
        for offset,value in [(-1,0),(0,256)]:
            with self.assertRaises(ValueError): model.set_u8(offset,value)
        a.write_bytes(b'invalid')
        with self.assertRaises(ValueError): model.load(a)

    def test_party_membership_reorders_records_and_preserves_home_ids(self):
        model=state.InitGamModel();model.bytes[0x2b5]=3;model.bytes[0x2d5]=2
        for i in range(16):model.bytes[2+i*32]=65+i;model.bytes[2+i*32+31]=0 if i<3 else 4
        model.set_party_member(5,True)
        self.assertEqual(model.bytes[2+3*32],70)
        self.assertEqual(model.u8(0x2b5),4)
        model.set_party_member(1,False,6)
        self.assertEqual(model.bytes[2+15*32],66)
        self.assertEqual(model.bytes[2+15*32+31],6)
        self.assertEqual(model.u8(0x2d5),1)
        with self.assertRaises(ValueError):model.set_party_member(0,False,4)

    def test_story_fixed_offsets_padding_and_limit(self):
        offsets=[0,0x111,0x3cb,0x590,0x803,0xac4,0xd6d,0xe23,0xfb6,0x1173,0x132a,0x14be,0x162e,0x192b,0x1b9a,0x1e7b,0x2172,0x244b,0x2780,0x2a97]
        raw=bytearray(11679)
        for offset in offsets:raw[offset:offset+6]=b'Hello\0'
        path=self.directory/'STORY.DAT';path.write_bytes(raw)
        original,segments=story.load_story_dat(path)
        self.assertEqual([s.start for s in segments],offsets)
        window=story.StoryEditorWindow(path);output=self.directory/'result.DAT';window._write_story_dat(output)
        self.assertEqual(output.read_bytes(),raw)
        segment=story.SegmentEditor(segments[0]);segment.edit.setPlainText('too long')
        self.assertEqual(segment.edit.toPlainText(),'Hello')
        window.segment_texts[0]='Hey';window._write_story_dat(output)
        self.assertEqual(output.read_bytes()[:6],b'Hey  \0')
        self.assertEqual(output.read_bytes()[6:],original[6:])
        with self.assertRaises(ValueError):story.normalize_to_ascii('bad\0text')

    @unittest.skipUnless((GAME/'TILES.16').exists(),'Optional original game resources unavailable')
    def test_original_resources_roundtrip(self):
        for path in GAME.glob('*.16'):
            with self.subTest(path=path.name):
                file=images.read_any16(path)
                blob=images.build_tiles_blob(file) if file.kind=='tiles' else images.build_multi_blob(file)
                raw=path.read_bytes()
                self.assertEqual(blob,lzw_decode(raw[4:],int.from_bytes(raw[:4],'little')))
        window=story.StoryEditorWindow(GAME/'STORY.DAT');output=self.directory/'STORY.DAT';window._write_story_dat(output)
        self.assertEqual(output.read_bytes(),(GAME/'STORY.DAT').read_bytes())
        model=state.InitGamModel(GAME/'INIT.GAM');before=bytes(model.bytes)
        window=state.MainWindow(str(GAME/'INIT.GAM'))
        self.assertEqual(bytes(window.model.bytes),before)

if __name__=='__main__':unittest.main()
