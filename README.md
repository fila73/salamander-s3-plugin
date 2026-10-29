# Amazon S3 Plugin for Open Salamander (x64)

A standalone **Amazon S3** and S3-compatible cloud object storage filesystem plugin for [Open Salamander 5.0](https://github.com/OpenSalamander/salamander) (x64).

> Copyright © 2026 fila-73  
> Based on Open Salamander SDK.

---

## Key Features

- **Broad Cloud Storage Compatibility**:
  - **AWS S3** (all standard AWS regions)
  - **MinIO** (self-hosted / on-premises)
  - **Cloudflare R2**
  - **Wasabi Hot Cloud Storage**
  - **Backblaze B2** (S3-compatible API)
  - **Ceph / LocalStack / DigitalOcean Spaces / Linode Object Storage**

- **Authentication & Security**:
  - Full **AWS Signature Version 4 (SigV4)** request signing implementation.
  - Native Windows Cryptography Next Generation (**`bcrypt.dll`**) for fast SHA-256 and HMAC-SHA256 calculation.
  - Support for Temporary Credentials & **STS Session Tokens** (`x-amz-security-token`).
  - Automatic detection and parsing of local **`~/.aws/credentials`** and **`~/.aws/config`** profiles (e.g., `[default]`, `[prod]`, `[stage]`).
  - Manual credential configuration with custom region and endpoint URL support.
  - Profile storage in the Windows Registry.

- **Filesystem & Object Operations**:
  - Root bucket listing (`s3:/`) displaying all accessible buckets.
  - Virtual folder navigation within buckets.
  - **Resumable Downloads & Uploads** with progress bar (using HTTP Range headers).
  - File and folder deletion (including bulk object deletion).
  - Create new buckets and virtual directories.
  - Rename and move objects via S3 `CopyObject` + `DeleteObject`.
  - Calculate folder sizes.
  - Edit remote files directly (F4 – downloads to temp, opens editor, auto-uploads on save).
  - Object metadata and properties viewer (StorageClass, ETag, Content-Length).

- **Modern Architecture & UI**:
  - Native high-performance HTTPS/HTTP networking via **WinHTTP** (`winhttp.dll`), supporting TLS 1.2/1.3 and system proxy settings.
  - Full Dark Mode integration matching Open Salamander theme.
  - Multi-language localization (`English.slg` and `Czech.slg`).

---

## Project Structure

| File / Directory | Description |
|------------------|-------------|
| `src/s3sigv4.h/.cpp` | AWS SigV4 request signing engine (Canonical Request, string to sign, HMAC SHA-256 derivation). |
| `src/awscred.h/.cpp` | Parser for `~/.aws/credentials` and `~/.aws/config` configuration files. |
| `src/s3conn.h/.cpp` | REST API communication layer over Windows WinHTTP (ListBuckets, ListObjectsV2 with continuation tokens, GetObject, PutObject, DeleteObject, HeadObject). |
| `src/s3glue.h/.cpp` | Integration glue between Open Salamander FS interface and `CS3Connection`. |
| `src/fs1.cpp` | Connection dialog (`ConnectDlgProc`), profile tree, and credential setup UI. |
| `src/fs2.cpp` | Main filesystem operations (ChangePath, ListCurrentPath, Copy, Delete, CreateDir, QuickRename, Properties). |
| `src/menu.cpp` | Menu command handlers (Disconnect, Calculate Size, Edit file). |
| `src/lang/` | English and Czech resource string definitions and dialog templates. |
| `test/test_sigv4.cpp` | Unit tests for SigV4 signing and AWS credential parsing. |

---

## Building

Requires Visual Studio (MSVC, x64) and the Open Salamander plugin SDK (`plugins\shared`). The SDK is looked up in `..\..\samandarin\src` relative to this repository; override with `/p:SalamanderSdkDir=<path to a directory containing plugins\shared>`.

```powershell
MSBuild src\vcxproj\s3.vcxproj /p:Configuration=Release /p:Platform=x64
```

Output goes to `build\Release_x64\`: `s3.spl`, `lang\english.slg` and `lang\czech.slg`.

---

## License
Open Salamander SDK (SPDX / GPL-2.0-or-later). Core custom components Copyright © 2026 fila-73.
