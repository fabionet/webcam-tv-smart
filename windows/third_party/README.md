# Librerie di terze parti (edizione Windows)

| Libreria | Versione | Licenza | Uso | Origine |
|---|---|---|---|---|
| minih264e.h | commit 207dee34 | CC0 1.0 | codifica H.264 (Baseline, SSE2) | https://github.com/lieff/minih264 |
| minimp4.h | commit 65fab0a0 | CC0 1.0 | contenitore MP4 | https://github.com/lieff/minimp4 |
| stb_image_write.h | v1.19 (commit 6b5799ba) | dominio pubblico / MIT | scrittura PNG (istantanee) | https://github.com/nothings/stb |
| vo-aacenc | 0.1.3 | Apache-2.0 | codifica AAC-LC | https://github.com/mstorsjo/vo-aacenc |
| libogg | 1.3.5 | BSD-3-Clause | contenitore Ogg | https://xiph.org/ogg/ |
| libvorbis | 1.3.7 | BSD-3-Clause | codifica Vorbis | https://xiph.org/vorbis/ |
| libtheora | 1.2.0 | BSD-3-Clause | codifica Theora | https://www.theora.org/ |

I tre header `minih264e.h`, `minimp4.h` e `stb_image_write.h` sono inclusi nel repository (dominio pubblico).
Le altre librerie vengono scaricate da `../fetch-deps.sh`, che ne verifica l'impronta SHA-256,
e compilate staticamente dentro l'eseguibile: l'applicazione non richiede DLL né codec esterni.
