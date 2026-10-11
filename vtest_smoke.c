/* Minimal vtest client: create venus context on virgl_test_server and
 * read back the venus capset (proof that vkr initialized MoltenVK). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>

static int sock;

static void w(const void *b, size_t n) {
    if (write(sock, b, n) != (ssize_t)n) { perror("write"); exit(2); }
}
static void r(void *b, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(sock, (char *)b + got, n - got);
        if (k <= 0) { perror("read"); exit(3); }
        got += (size_t)k;
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/Users/herbcheng/venus-stack/vtest.sock";
    struct sockaddr_un un;
    memset(&un, 0, sizeof(un));
    un.sun_family = AF_UNIX;
    strncpy(un.sun_path, path, sizeof(un.sun_path) - 1);
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(sock, (struct sockaddr *)&un, sizeof(un)) < 0) { perror("connect"); return 1; }
    printf("connected to %s\n", path);

    uint32_t hdr[2];
    /* 1. CREATE_RENDERER (len field is in BYTES for this command) */
    const char *name = "vtest-smoke";
    uint32_t namelen = (uint32_t)strlen(name) + 1;
    hdr[0] = namelen; hdr[1] = 8;
    w(hdr, sizeof(hdr)); w(name, namelen);
    printf("create_renderer sent\n");

    /* 2. PROTOCOL_VERSION {1,11} + {3}; read {1,11} + {server_ver} */
    hdr[0] = 1; hdr[1] = 11;
    uint32_t ver = 3;
    w(hdr, sizeof(hdr)); w(&ver, 4);
    uint32_t rhdr[2], rver;
    r(rhdr, 8); r(&rver, 4);
    printf("protocol: server=%u hdr(len=%u id=%u)\n", rver, rhdr[0], rhdr[1]);

    /* 3. CONTEXT_INIT {1,17} + {capset_id=VENUS=4} (no reply) */
    hdr[0] = 1; hdr[1] = 17;
    uint32_t capset = 4;
    w(hdr, sizeof(hdr)); w(&capset, 4);
    printf("context_init sent (capset_id=%u VENUS)\n", capset);

    /* 4. GET_CAPSET {2,16} + {id=4, version=0} */
    hdr[0] = 2; hdr[1] = 16;
    uint32_t gc[2] = {4, 0};
    w(hdr, sizeof(hdr)); w(gc, 8);
    r(rhdr, 8);
    printf("get_capset resp: len=%u dwords id=%u\n", rhdr[0], rhdr[1]);
    if (rhdr[1] != 16) { printf("UNEXPECTED_ID\n"); return 4; }
    uint32_t ndata = rhdr[0];
    uint32_t *data = malloc(ndata * 4);
    r(data, ndata * 4);
    if (!data[0]) { printf("CAPSET_UNSUPPORTED (data_start=false)\n"); return 5; }
    printf("capset payload dwords=%u, first dwords:", ndata - 1);
    for (uint32_t i = 1; i < (ndata < 9 ? ndata : 9); i++) printf(" %08x", data[i]);
    printf("\nSMOKE_OK: venus capset returned, vkr+MoltenVK initialized\n");
    free(data);
    close(sock);
    return 0;
}
