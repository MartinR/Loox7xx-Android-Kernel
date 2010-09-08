#ifndef _ACX_GENERIC_H
#define _ACX_GENERIC_H

void acx_l_reset_mac(acx_device_t *adev);
int acx_s_upload_fw(acx_device_t * adev);
int acx_s_write_fw(acx_device_t * adev, const firmware_image_t *fw_image, u32 offset);
int acx_s_validate_fw(acx_device_t * adev, const firmware_image_t *fw_image, u32 offset);
int acx_s_verify_init(acx_device_t * adev);


#endif
