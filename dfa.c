#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <minix/ds.h>
#include <minix/ioctl.h>
#include <sys/ioc_dfa.h>

const int CHAR_SIZE = 256;
unsigned char current_state;
unsigned char accepting_states[CHAR_SIZE];
unsigned char transition[CHAR_SIZE][CHAR_SIZE];

const int BUF_SIZE = 4000;
char buffer[BUF_SIZE];

/*
 * Function prototypes for the hello driver.
 */
static ssize_t dfa_read(devminor_t minor, u64_t position, endpoint_t endpt,
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static ssize_t dfa_write(devminor_t minor, u64_t position, endpoint_t endpt, 
    cp_grant_id_t grant, size_t size, int flags, cdev_id_t id);
static int dfa_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
    cp_grant_id_t grant, int flags, endpoint_t user_endpt, cdev_id_t id);

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
    .cdr_ioctl  = dfa_ioctl
};

size_t min(size_t a, size_t b) {
    return (a<b)?a:b;
}

static ssize_t dfa_read(devminor_t UNUSED(minor), u64_t position,
    endpoint_t endpt, cp_grant_id_t grant, size_t size, int UNUSED(flags),
    cdev_id_t UNUSED(id))
{
    char *ptr;
    int ret;

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
            unsigned char p = buffer[i];
            current_state = transition[current_state][p]; 
        }

        bytes_written += chunk;
    }

    /* Return the number of bytes written. */
    return size;
}

static int sef_cb_lu_state_save(int UNUSED(state)) {
/* Save the state. */
    int rc;
    rc = ds_publish_mem("dfa_current_state", &current_state, 1, DSF_OVERWRITE);
    if (rc != OK) return rc;
    rc = ds_publish_mem("dfa_accepting_states", (char*)accepting_states, CHAR_SIZE, DSF_OVERWRITE);
    if (rc != OK) return rc;
    rc = ds_publish_mem("dfa_transition", (char*)transition, CHAR_SIZE * CHAR_SIZE, DSF_OVERWRITE);
    if (rc != OK) return rc;

    return OK;
}

static int lu_state_restore() {
/* Restore the state. */
    size_t len;
    int rc;

    len = 1;
    rc = ds_retrieve_mem("dfa_current_state", &current_state, &len);
    if (rc != OK) return rc; 
    rc = ds_delete_mem("dfa_current_state");
    if (rc != OK) return rc; 

    len = CHAR_SIZE;
    rc = ds_retrieve_mem("dfa_accepting_states", (char*)accepting_states, &len);
    if (rc != OK) return rc; 
    rc = ds_delete_mem("dfa_accepting_states");
    if (rc != OK) return rc; 

    len = CHAR_SIZE * CHAR_SIZE;
    rc = ds_retrieve_mem("dfa_transition", (char*)transition, &len);
    if (rc != OK) return rc; 
    rc = ds_delete_mem("dfa_transition");
    if (rc != OK) return rc; 

    return OK;
}

static int dfa_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
    cp_grant_id_t grant, int flags, endpoint_t user_endpt, cdev_id_t id) {
    int rc;
    char buf[3];

    switch (request) {
    case DFAIOCRESET: 
        current_state = 0;
        rc = OK;
        break;

    case DFAIOCADD: 
        rc = sys_safecopyfrom(endpt, grant, 0, (vir_bytes) buf, 3);
        if (rc == OK) {
            unsigned char p = buf[0];
            unsigned char a = buf[1];
            unsigned char q = buf[2];
            transition[p][a] = q;
            current_state = 0;
        }
        break;

    case DFAIOCACCEPT: 
        rc = sys_safecopyfrom(endpt, grant, 0, (vir_bytes) buf, 1);
        if (rc == OK) {
            unsigned char p = buf[0];
            accepting_states[p] = 1;
        }
        break;

    case DFAIOCREJECT: 
        rc = sys_safecopyfrom(endpt, grant, 0, (vir_bytes) buf, 1);
        if (rc == OK) {
            unsigned char p = buf[0];
            accepting_states[p] = 0;
        }
        break;

    default:
        rc = ENOTTY;    
    }

    return rc;
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
        break;

        case SEF_INIT_LU:
            /* Restore the state. */
            lu_state_restore();
            do_announce_driver = FALSE;
        break;

        case SEF_INIT_RESTART:
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

