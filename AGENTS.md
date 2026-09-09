# STAR contribution guidance

- Keep source, data descriptions and documentation in UTF-8.
- Preserve user saves and unrelated work. Put temporary work in `work` and built releases in `outputs`.
- Keep astronomical doubles in meters separate from local Unreal centimeters. Test frame conversions.
- Check the behavior affected by a change. A source build is not proof of packaged graphics, physical input or VR.
- Keep source URLs, observation dates, units, coordinate frames and license notices with imported data.
- Do not commit credentials, personal logs, saves, generated engine files or editor binaries.
- Run one editor/build writer per checkout. Use independent checkouts for parallel work.
