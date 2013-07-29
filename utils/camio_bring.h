/*
 * camio_bring.h
 *
 *  Created on: Mar 26, 2013
 *      Author: mgrosvenor
 */

#ifndef CAMIO_BRING_H_
#define CAMIO_BRING_H_

#define CAMIO_BRING_SLOT_COUNT_DEFAULT (1024)
#define CAMIO_BRING_SLOT_SIZE_DEFAULT (4 * 1024)  //4K
#define CAMIO_BRING_SLOT_AVAIL  (priv->slot_size - 2*sizeof(uint64_t))
#define CAMIO_BRING_MEM_SIZE    (priv->slot_size * priv->slot_count + 2*sizeof(uint64_t))


#define CHECK_LEN_OK(len) \
    if(len > CAMIO_BRING_SLOT_AVAIL){ \
        eprintf_exit("Length supplied (%lu) is greater than slot size (%lu, corruption is likely.\n", len, CAMIO_BRING_SLOT_AVAIL ); \
    }


#define bring_ostream_created (*(volatile uint64_t*)(priv->bring + priv->slot_count * priv->slot_size + sizeof(uint64_t)) )
#define bring_istream_connected (*(volatile uint64_t*)(priv->bring + priv->slot_count * priv->slot_size ))

#endif /* CAMIO_BRING_H_ */
