#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

int vktut2_create(void* view, int width, int height, float backingScaleFactor);
void vktut2_drawframe(unsigned int frameIndex);
void vktut2_destroy(void);

#ifdef __cplusplus
}
#endif
