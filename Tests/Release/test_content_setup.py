"""Exercise archive extraction safety and preservation of edited project assets."""
from pathlib import Path
import hashlib,json,shutil,subprocess,tempfile,unittest,zipfile
ROOT=Path(__file__).resolve().parents[2]

class ContentSetupTests(unittest.TestCase):
 def setUp(self):
  (ROOT/'work').mkdir(exist_ok=True)
  self.temp=tempfile.TemporaryDirectory(dir=ROOT/'work');self.root=Path(self.temp.name)
  (self.root/'Tools').mkdir();(self.root/'Data').mkdir()
  shutil.copy2(ROOT/'Tools/Setup-Content.ps1',self.root/'Tools/Setup-Content.ps1')
 def tearDown(self):self.temp.cleanup()
 def run_setup(self,files):
  archive=self.root/'fixture.zip'
  with zipfile.ZipFile(archive,'w') as z:
   for name,body in files:z.writestr(name,body)
  manifest={'url':'https://example.invalid/not-used','sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
  (self.root/'Data/content-release.json').write_text(json.dumps(manifest),encoding='utf-8')
  return subprocess.run(['pwsh','-NoProfile','-File',str(self.root/'Tools/Setup-Content.ps1'),'-ArchivePath',str(archive)],capture_output=True,text=True,encoding='utf-8')
 def test_clean_install_and_idempotent_readback(self):
  files=[('Content/Star/test.txt','original'),('Art/model.txt','shape')]
  self.assertEqual(self.run_setup(files).returncode,0)
  self.assertEqual(self.run_setup(files).returncode,0)
  self.assertEqual((self.root/'Content/Star/test.txt').read_text(),'original')
 def test_edited_asset_prevents_any_extraction(self):
  target=self.root/'Content/Star/test.txt';target.parent.mkdir(parents=True);target.write_text('edited')
  result=self.run_setup([('Art/new.txt','new'),('Content/Star/test.txt','upstream')])
  self.assertNotEqual(result.returncode,0)
  self.assertEqual(target.read_text(),'edited');self.assertFalse((self.root/'Art/new.txt').exists())
 def test_traversal_prevents_any_extraction(self):
  result=self.run_setup([('Content/ok.txt','ok'),('Content/../../escaped.txt','bad')])
  self.assertNotEqual(result.returncode,0)
  self.assertFalse((self.root/'Content/ok.txt').exists());self.assertFalse((self.root.parent/'escaped.txt').exists())
 def test_second_archive_conflict_prevents_first_archive_writes(self):
  cache=self.root/'work/downloads';cache.mkdir(parents=True)
  specs=[]
  for name,entry in [('core.zip','Content/new.txt'),('art.zip','Art/edited.txt')]:
   archive=cache/name
   with zipfile.ZipFile(archive,'w') as z:z.writestr(entry,'release')
   specs.append({'name':name,'url':'https://example.invalid/not-used','sha256':hashlib.sha256(archive.read_bytes()).hexdigest()})
  (self.root/'Data/content-release.json').write_text(json.dumps({'archives':specs}),encoding='utf-8')
  edited=self.root/'Art/edited.txt';edited.parent.mkdir();edited.write_text('my work')
  result=subprocess.run(['pwsh','-NoProfile','-File',str(self.root/'Tools/Setup-Content.ps1')],capture_output=True)
  self.assertNotEqual(result.returncode,0);self.assertFalse((self.root/'Content/new.txt').exists());self.assertEqual(edited.read_text(),'my work')

 def test_duplicate_target_prevents_any_extraction(self):
  result=self.run_setup([('Art/new.txt','new'),('Content/same.txt','one'),('Content/same.txt','two')])
  self.assertNotEqual(result.returncode,0)
  self.assertFalse((self.root/'Art/new.txt').exists());self.assertFalse((self.root/'Content/same.txt').exists())
 def test_file_directory_collision_prevents_any_extraction(self):
  for paths in [('Content/item','Content/item/child'),('Content/item/child','Content/item')]:
   with self.subTest(paths=paths):
    result=self.run_setup([('Art/new.txt','new'),(paths[0],'one'),(paths[1],'two')])
    self.assertNotEqual(result.returncode,0);self.assertFalse((self.root/'Art/new.txt').exists())
 def test_existing_file_ancestor_prevents_any_extraction(self):
  blocker=self.root/'Content/item';blocker.parent.mkdir();blocker.write_text('mine')
  result=self.run_setup([('Art/new.txt','new'),('Content/item/child','release')])
  self.assertNotEqual(result.returncode,0);self.assertEqual(blocker.read_text(),'mine')
  self.assertFalse((self.root/'Art/new.txt').exists())
 def test_windows_path_aliases_are_rejected_before_writes(self):
  for path in ['Content/file:stream','Content/CON.txt','Content/NUL','Content/trailing.',
               'Content/trailing /file','Content/./file','Content//file','content/alias.txt']:
   with self.subTest(path=path):
    result=self.run_setup([('Art/new.txt','new'),(path,'bad')])
    self.assertNotEqual(result.returncode,0);self.assertFalse((self.root/'Art/new.txt').exists())
 def test_case_colliding_files_prevent_any_extraction(self):
  result=self.run_setup([('Art/new.txt','new'),('Content/Case.txt','one'),('Content/case.txt','two')])
  self.assertNotEqual(result.returncode,0);self.assertFalse((self.root/'Art/new.txt').exists())
 def test_content_junction_cannot_escape_project(self):
  import os
  with tempfile.TemporaryDirectory(dir=ROOT/'work') as outside:
   link=self.root/'Content'
   if os.name=='nt':
    made=subprocess.run(['cmd','/c','mklink','/J',str(link),outside],capture_output=True)
    self.assertEqual(made.returncode,0,made.stderr.decode(errors='replace'))
   else:
    link.symlink_to(outside,target_is_directory=True)
   try:
    result=self.run_setup([('Art/new.txt','new'),('Content/escaped.txt','bad')])
    self.assertNotEqual(result.returncode,0)
    self.assertFalse((Path(outside)/'escaped.txt').exists());self.assertFalse((self.root/'Art/new.txt').exists())
   finally:
    if os.name=='nt':os.rmdir(link)
    else:link.unlink()
 def test_valid_explicit_directories_are_idempotent(self):
  files=[('Content/',''),('Content/nested/',''),('Content/nested/asset.txt','good'),('Art/','')]
  for _ in range(2):
   result=self.run_setup(files);self.assertEqual(result.returncode,0,result.stderr)
  self.assertEqual((self.root/'Content/nested/asset.txt').read_text(),'good')

if __name__=='__main__':unittest.main(verbosity=2)
