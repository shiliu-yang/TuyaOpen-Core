/**
 * @file tal_memory.h
 * @brief TAL heap interface: tal_malloc / tal_free / tal_calloc, the
 *        Malloc / Calloc / Free aliases, and the optional external-RAM pair.
 *
 * tal_malloc and tal_free dispatch to the TAL_API_OPS_T .malloc / .free
 * members assembled by the port. tal_calloc allocates through tal_malloc,
 * zeroes the block and returns NULL on a zero size or a nitems * size
 * overflow. tal_ex_ram_malloc / tal_ex_ram_free exist only when ENABLE_EXT_RAM
 * is enabled; they are the ex-RAM card members and provide no zeroing.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TAL_MEMORY_H__
#define __TAL_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************************
 ********************* constant ( macro and enum ) *********************
 **********************************************************************/
#define Malloc(req_size)             tal_malloc(req_size)
#define Calloc(req_count, req_size)  tal_calloc(req_count, req_size)
#define Free(ptr)                    tal_free(ptr)

/***********************************************************************
 ********************* struct ******************************************
 **********************************************************************/

/***********************************************************************
 ********************* variable ****************************************
 **********************************************************************/

/***********************************************************************
 ********************* function ****************************************
 **********************************************************************/

/**
 * @brief This API is used to alloc memory of system.
 *
 * @param[in] size: memory size
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void *tal_malloc(size_t size);

/**
 * @brief This API is used to free memory of system.
 *
 * @param[in] ptr: memory point
 *
 * @return OPRT_OK on success. Others on error, please refer to
 * tuya_error_code.h
 */
void tal_free(void *ptr);

/**
 * @brief Allocate and clear the memory
 *
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 *
 * @return the zeroed memory address, or NULL if the allocation fails,
 *         nitems/size is 0, or nitems * size overflows
 */
void *tal_calloc(size_t nitems, size_t size);

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
/**
 * @brief Allocate and free external RAM (e.g. PSRAM).
 *
 * The implementation is provided by the port through the ex_ram_* members of
 * TAL_API_OPS_T (macro-isolated: the interfaces do not exist when
 * ENABLE_EXT_RAM is disabled). Only malloc/free are provided -- callers that
 * need zeroed memory must memset it themselves.
 *
 * @param[in] size: memory size (tal_ex_ram_malloc)
 * @param[in] ptr:  memory point (tal_ex_ram_free)
 *
 * @return external RAM address, or NULL if the allocation fails
 */
void *tal_ex_ram_malloc(size_t size);
void tal_ex_ram_free(void *ptr);
#endif


#ifdef __cplusplus
}
#endif

#endif /* __TAL_MEMORY_H__ */
