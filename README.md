# CRIU — link-remap reuse fork

This is a fork of [checkpoint-restore/criu](https://github.com/checkpoint-restore/criu).
The upstream CRIU README is preserved verbatim as
[`README.original.md`](README.original.md).

The default branch, **`snapshot/link-remap-reusable`**, adds two changes on top of
upstream `criu-dev` (`33482a1`):

## Recreate a missing link-remap source so a checkpoint image can be restored more than once

**Problem.** With `--link-remap`, at dump time CRIU creates a temporary hard link
`link_remap.<id>` next to a multi-linked file (e.g. the POSIX semaphores
`/dev/shm/sem.*` that Python `multiprocessing` creates). The first restore
consumes/renames that temp, but its contents are not stored in the image, so a
second restore of the same image fails with:

```
Can't link dev/shm/link_remap.<id> -> dev/shm/sem.<name>: No such file or directory
```

i.e. **checkpoint images were effectively one-shot** for such files.

**Fix** (`criu/files-reg.c`, `rfi_remap()`): if the link-remap source is missing
(`ENOENT`), recreate a placeholder of the expected size from the dumped
`RegFileEntry` and retry the link. The real content is written from the dumped
pages once the file is opened, so the restored process is unchanged.

**Effect.** A checkpoint image can be restored repeatedly. Verified with a warm
vLLM worker (whose engine uses `/dev/shm` POSIX semaphores): the same image
restored twice, both times to a correct inference.

## Recreate a missing regular file on a memory filesystem so images are reusable and node-portable

**Problem.** Some regular files live on a memory filesystem, notably POSIX
shared memory under `/dev/shm` (e.g. Python
`multiprocessing.shared_memory` segments named `psm_*`, which vLLM's engine and
tensor-parallel workers use for IPC). Such a file is *not* a ghost, so CRIU
stores only its `RegFileEntry` metadata (size/mode) plus the mapped pages and
assumes the path keeps existing. That holds for a same-host restore immediately
after the dump (the dump leaves the file behind), but not for:

- a **second restore** of the same images (the first restored process unlinks the
  segment on exit), or
- a restore on **another node / in another container** (the file never existed
  there),

and the restore fails with:

```
Error (criu/files-reg.c): Can't open file dev/shm/psm_<hex> on restore: No such file or directory
```

i.e. images with such files are effectively **one-shot and node-bound**.

**Fix** (`criu/files-reg.c`, `do_open_reg_noseek_flags()`): if the open fails with
`ENOENT` for a non-remap, non-external regular file that has a recorded size,
recreate a placeholder with the recorded size/mode and retry the open. The real
content is written back from the dumped pages once the file is mapped;
`validate_file()` still checks size and mode, so a genuine mismatch is still
reported.

**Effect.** Checkpoint images containing `/dev/shm` shared memory can be restored
repeatedly and on another node. Verified with a warm vLLM **TP=2** worker
(4× Tesla T4, driver 580, NCCL over SHM/IPC): the same image now restores more
than once, and TP=2 restore no longer fails on `dev/shm/psm_*`.

## Build / install

```bash
git clone git@github.com:eliird/CRIU-multiprocess.git   # this fork
cd CRIU-multiprocess
sudo apt-get install -y build-essential pkg-config protobuf-c-compiler \
  libprotobuf-c-dev libprotobuf-dev protobuf-compiler libnl-3-dev \
  libnl-route-3-dev libnet1-dev libcap-dev python3-protobuf libbsd-dev \
  uuid-dev libaio-dev iproute2
make -j"$(nproc)" all
sudo make install-lib install-crit install-criu install-compel install-cuda_plugin
sudo install -m 0755 plugins/cuda/cuda_plugin.so /usr/lib/criu/cuda_plugin.so
```

(`make install` also builds man pages and needs `asciidoc`.)

To apply just the change to upstream CRIU instead, use the extracted patch:
`patches/criu-link-remap-reusable.patch` in
[eliird/vllm-snapshot](https://github.com/eliird/vllm-snapshot).

## Optional: remap CUDA devices on restore (`CRIU_CUDA_DEVICE_MAP`)

Restoring a CUDA checkpoint onto a different set of GPUs (another device set or
node) requires `cuda-checkpoint --device-map oldUuid1=newUuid1,...`. The CUDA
plugin never passed it. If `CRIU_CUDA_DEVICE_MAP` is set in the `criu restore`
process environment, the plugin now appends `--device-map <value>` for the
restore action. UUIDs use cuda-checkpoint's own format
(`GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`).
