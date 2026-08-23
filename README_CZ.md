# Amazon S3 Plugin pro Open Salamander

Samostatný souborový plugin (FS) pro Open Salamander umožňující přímou práci s **Amazon S3** a S3-kompatibilními cloudovými úložišti (MinIO, Wasabi, Cloudflare R2, Backblaze B2, Ceph, LocalStack).

## Klíčové vlastnosti

- **AWS Signature Version 4 (SigV4) autentizace**: Implementována pomocí nativního Windows Cryptography Next Generation (`bcrypt.dll`).
- **Nativní síťová vrstva WinHTTP**: Rychlé a bezpečné HTTPS/HTTP přenosy (`winhttp.dll`), podpora TLS 1.2/1.3 a systémových proxy.
- **Správa přihlašovacích údajů**:
  - Přímé načítání profilů z `~/.aws/credentials` a `~/.aws/config` (např. `[default]`, `[test]`, `[stage]`, ...).
  - Možnost manuálního zadání *Access Key ID*, *Secret Access Key*, *Session Token (STS)*, *Region* a *Custom Endpoint*.
  - Ukládání vlastních pojmenovaných relací do registru.
- **Souborové operace**:
  - Výpis všech dostupných bucketů na kořenové úrovni (`s3:/`).
  - Procházení virtuálních adresářů uvnitř bucketu.
  - Stahování souborů s podporou navazování (HTTP Range).
  - Nahrávání souborů a složek na S3.
  - Mazání souborů, složek a celých bucketů.
  - Vytváření nových bucketů a virtuálních složek.
  - Přejmenování a přesun objektů přes S3 `CopyObject` + `DeleteObject`.
  - Zobrazení metadat (StorageClass, ETag).

## Struktura projektu

- `src/s3sigv4.h`, `src/s3sigv4.cpp` – AWS SigV4 výpočet podpisů (SHA256, HMAC-SHA256, Canonical Request).
- `src/awscred.h`, `src/awscred.cpp` – Parser `~/.aws/credentials` a `~/.aws/config`.
- `src/s3conn.h`, `src/s3conn.cpp` – REST API klient přes WinHTTP.
- `src/s3glue.h`, `src/s3glue.cpp` – Cesty a integrace se Salamanderem.
- `src/fs1.cpp`, `src/fs2.cpp` – Implementace FS rozhraní a přihlašovacího dialogu.
- `src/vcxproj/` – MSBuild projektové soubory (`s3.vcxproj`, `lang_s3.vcxproj`).
