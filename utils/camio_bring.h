/*
 * camio_bring.h
 *
 *  Created on: Mar 26, 2013
 *      Author: mgrosvenor
 */

#ifndef CAMIO_BRING_H_
#define CAMIO_BRING_H_

#define CAMIO_BRING_SLOT_COUNT (1024)
#define CAMIO_BRING_SLOT_SIZE (4 * 1024)  //4K
#define CAMIO_BRING_SLOT_AVAIL (CAMIO_BRING_SLOT_SIZE - sizeof(uint64_t) * 2)
#define CAMIO_BRING_MEM_SIZE ( CAMIO_BRING_SLOT_COUNT * CAMIO_BRING_SLOT_SIZE + sizeof(uint64_t))


#define CHECK_LEN_OK(len) \
    if(len > CAMIO_BRING_SLOT_AVAIL){ \
        eprintf_exit("Length supplied (%lu) is greater than slot size (%lu, corruption is likely.\n", len, CAMIO_BRING_SLOT_AVAIL ); \
    }


#define bring_connected (*(volatile uint64_t*)(priv->bring + CAMIO_BRING_SLOT_COUNT * CAMIO_BRING_SLOT_SIZE))

#endif /* CAMIO_BRING_H_ */
