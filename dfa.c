#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <sys/ioc_dfa.h>

#define HELLO_MESSAGE "Hello, DFA!\n"

const int CHAR_SIZE = 256;
char current_state;
char accepting_states[CHAR_SIZE];
char transition[CHAR_SIZE][CHAR_SIZE];

const int BUF_SIZE = 4; // TODO Increase after tests
char buffer[BUF_SIZE];

/*
 * Function prototypes for the hello driver.
 */
static ssize_t dfa_read(devminor_t minor, u64_t position, endpoint_t endpt,
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static ssize_t dfa_write(devminor_t minor, u64_t position, endpoint_t endpt, 
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);

/* SEF functions and variables. */
static void sef_local_startup(void);
static int sef_cb_init(int type, sef_init_info_t *info);
static int sef_cb_lu_state_save(int);
static int lu_state_restore(void);

/* Entry points to the hello driver. */
static struct chardriver hello_tab =
{
    .cdr_read	= dfa_read,
    .cdr_write  = dfa_write,
};

size_t min(size_t a, size_t b) {
    return (a<b)?a:b;
}

static ssize_t dfa_read(devminor_t UNUSED(minor), u64_t position,
    endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
    cdev_id_t UNUSED(id))
{
//    u64_t dev_size;
    char *ptr;
    int ret;
//    char *buf = HELLO_MESSAGE;

//    printf("hello_read()\n");

    /* This is the total size of our device. */
//    dev_size = (u64_t) strlen(buf);

    /* Check for EOF, and possibly limit the read size. */
//    if (position >= dev_size) return 0;		/* EOF */
//    if (position + size > dev_size)
//        size = (size_t)(dev_size - position);	/* limit size */

    if (accepting_states[current_state] == 1) {
        memset(buffer, 'Y', min(BUF_SIZE, size));
    } else {
        memset(buffer, 'N', min(BUF_SIZE, size));
    }

    int bytes_read = 0;

    while (bytes_read < size) {
        int chunk = min(BUF_SIZE, size - bytes_read);

        /* Copy the requested part to the caller. */
        if ((ret = sys_safecopyto(endpt, grant, (vir_bytes)bytes_read, (vir_bytes)buffer, chunk)) != OK)
            return ret;

        bytes_read += chunk;
    }

    /* Return the number of bytes read. */
    return size;
}

static ssize_t dfa_write(devminor_t UNUSED(minor), u64_t position,
    endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
    cdev_id_t UNUSED(id))
{
    int ret;
    int bytes_written = 0;

    while (bytes_written < size) {
        int chunk = min(BUF_SIZE, size - bytes_written);

        /* Copy the requested part from the caller. */
        if ((ret = sys_safecopyfrom(endpt, grant, (vir_bytes)bytes_written, (vir_bytes)buffer, chunk)) != OK)
            return ret;
        
        /* Make transitions */
        for (int i = 0; i < chunk; i++) {
            current_state = transition[current_state][buffer[i]]; 
        }

        bytes_written += chunk;
    }

    /* Return the number of bytes written. */
    return size;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
/* Save the state. */
//    ds_publish_u32("open_counter", open_counter, DSF_OVERWRITE);

    return OK;
}

static int lu_state_restore() {
/* Restore the state. */
    u32_t value;

//    ds_retrieve_u32("open_counter", &value);
//    ds_delete_u32("open_counter");
//    open_counter = (int) value;

    return OK;
}

static void sef_local_startup()
{
    /*
     * Register init callbacks. Use the same function for all event types
     */
    sef_setcb_init_fresh(sef_cb_init);
    sef_setcb_init_lu(sef_cb_init);
    sef_setcb_init_restart(sef_cb_init);

    /*
     * Register live update callbacks.
     */
    /* - Agree to update immediately when LU is requested in a valid state. */
    sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
    /* - Support live update starting from any standard state. */
    sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);
    /* - Register a custom routine to save the state. */
    sef_setcb_lu_state_save(sef_cb_lu_state_save);

    /* Let SEF perform startup. */
    sef_startup();
}

static int sef_cb_init(int type, sef_init_info_t *UNUSED(info))
{
/* Initialize the dfa driver. */
    current_state = 0;
    memset(accepting_states, 0, CHAR_SIZE);
    memset(transition, 0, CHAR_SIZE * CHAR_SIZE);

    int do_announce_driver = TRUE;

    switch(type) {
        case SEF_INIT_FRESH:
            printf("%s", HELLO_MESSAGE);
        break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;

            printf("%sHey, I'm a new version!\n", HELLO_MESSAGE);
        break;

        case SEF_INIT_RESTART:
            printf("%sHey, I've just been restarted!\n", HELLO_MESSAGE);
        break;
    }

    /* Announce we are up when necessary. */
    if (do_announce_driver) {
        chardriver_announce();
    }

    /* Initialization completed successfully. */
    return OK;
}

int main(void)
{
    /*
     * Perform initialization.
     */
    sef_local_startup();

    /*
     * Run the main loop.
     */
    chardriver_task(&hello_tab);
    return OK;
}

