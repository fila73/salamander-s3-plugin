# Amazon S3 Plugin pro Open Salamander (x64)

Samostatný souborový plugin (FS) pro [Open Salamander 5.0](https://github.com/OpenSalamander/salamander) (x64) umožňující přímou práci s **Amazon S3** a S3-kompatibilními cloudovými úložišti.

> Copyright © 2026 fila-73  
> Vychází ze šablony SDK Open Salamander.

---

## Klíčové vlastnosti

- **Široká kompatibilita s cloudovými úložišti**:
  - **AWS S3** (všechny standardní AWS regiony)
  - **MinIO** (on-premise / privátní instalace)
  - **Cloudflare R2**
  - **Wasabi Hot Cloud Storage**
  - **Backblaze B2** (S3 kompatibilní rozhraní)
  - **Ceph / LocalStack / DigitalOcean Spaces / Linode Object Storage**

- **Autentizace a bezpečnost**:
  - Kompletní implementace podepisování požadavků **AWS Signature Version 4 (SigV4)**.
  - Využití nativního Windows Cryptography Next Generation (**`bcrypt.dll`**) pro hardwarově akcelerované SHA-256 a HMAC-SHA256 výpočty bez nutnosti externích knihoven.
  - Podpora dočasných přihlašovacích údajů a **STS Session Tokenů** (`x-amz-security-token`).
  - Automatická detekce a načítání existujících profilů z **`~/.aws/credentials`** a **`~/.aws/config`** (např. `[default]`, `[prod]`, `[stage]`).
  - Možnost manuální konfigurace Access Key ID, Secret Access Key, vlastního regionu a Endpoint URL.
  - Ukládání vlastních profilů do systémového registru Windows.

- **Souborové operace a správa objektů**:
  - Výpis všech dostupných bucketů na kořenové úrovni (`s3:/`).
  - Navigace ve virtuálních složkách uvnitř bucketů.
  - **Stahování a nahrávání s podporou navazování** (resume přes HTTP Range hlavičky) a zobrazením průběhu.
  - Mazání souborů, složek i celých bucketů.
  - Vytváření nových bucketů a virtuálních adresářů.
  - Přejmenování a přesun objektů přes S3 `CopyObject` + `DeleteObject`.
  - Výpočet velikosti složek na S3.
  - Přímá úprava souborů (F4 – stažení do dočasné složky, otevření v editoru, automatický upload po uložení).
  - Zobrazení vlastností a metadat objektů (StorageClass, ETag, velikost).

- **Moderní architektura a vzhled**:
  - Nativní a rychlá síťová vrstva přes **WinHTTP** (`winhttp.dll`) s podporou TLS 1.2/1.3 a systémových proxy.
  - Plná integrace tmavého režimu (Dark Mode) sjednoceného se stylem Open Salamandera.
  - Jazyková lokalizace (`Czech.slg` a `English.slg`).

---

## Struktura projektu

| Soubor / Adresář | Popis |
|------------------|-------|
| `src/s3sigv4.h/.cpp` | Generátor SigV4 podpisů (Canonical Request, String to Sign, HMAC SHA-256). |
| `src/awscred.h/.cpp` | Parser konfiguračních souborů `~/.aws/credentials` a `~/.aws/config`. |
| `src/s3conn.h/.cpp` | REST API komunikace přes WinHTTP (ListBuckets, ListObjectsV2 s pokračovacími tokeny, GetObject, PutObject, DeleteObject, HeadObject). |
| `src/s3glue.h/.cpp` | Lepidlo propojující rozhraní Salamandera s `CS3Connection`. |
| `src/fs1.cpp` | Přihlašovací dialog (`ConnectDlgProc`), správa profilů a zadávání klíčů. |
| `src/fs2.cpp` | Implementace souborových operací (ChangePath, ListCurrentPath, Copy, Delete, CreateDir, QuickRename, Properties). |
| `src/s3darkmode.cpp` | Vykreslování a stylování dialogů v tmavém režimu. |
| `src/menu.cpp` | Obsluha položek menu (Odpojit, Spočítat velikost, Úprava souboru). |
| `src/lang/` | České a anglické textové zdroje a dialogové šablony pro `.slg`. |
| `test/test_sigv4.cpp` | Jednotkové testy pro SigV4 podpis a parsování AWS profilů. |

---

## Sestavení

### Možnost A: Přes MinGW-w64 (WSL / MSYS2)
```bash
make -f Makefile.mingw
```

Příkaz vytvoří `s3.spl`, `Czech.slg` a `English.slg`.

### Možnost B: Přes Visual Studio (MSBuild)
```powershell
MSBuild src\vcxproj\s3.vcxproj /p:Configuration=Release /p:Platform=x64
```

---

## Licence
Postaveno na Open Salamander SDK (SPDX / GPL-2.0-or-later). Vlastní komponenty Copyright © 2026 fila-73.
