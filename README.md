# Vulkanstein3D
Exercise in using the Vulkan API

## Notes

### Semaphore per swapchain image

The `renderFinished` semaphore must be **one per swapchain image**, not a single shared semaphore.

The presentation engine holds a reference to the semaphore passed to `vkQueuePresentKHR` until the
associated image is re-acquired. If a single semaphore is reused across frames, the next
`vkQueueSubmit` may try to signal it while the presentation engine still holds it from a previous
image, causing `VUID-vkQueueSubmit-pSignalSemaphores-00067`.

Indexing `renderFinished` semaphores by the acquired image index ensures each presentation holds a
distinct semaphore, which is only reused once that same image is re-acquired.

See: https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html
