# CRIU — link-remap reuse fork

This is a fork of [checkpoint-restore/criu](https://github.com/checkpoint-restore/criu).
The upstream CRIU README is preserved verbatim as
[`README.original.md`](README.original.md).

The default branch, **`snapshot/link-remap-reusable`**, adds one change on top of
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
