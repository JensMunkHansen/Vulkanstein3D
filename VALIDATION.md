[2026-02-06 13:43:27.328] [trace] Swapchain recreated: 1266x863
Validation Error: [ VUID-vkQueueSubmit-pSignalSemaphores-00067 ] | MessageID = 0x539277af
vkQueueSubmit(): pSubmits[0].pSignalSemaphores[0] (VkSemaphore 0x7b000000007b) is being signaled by VkQueue 0x193ee99e560, but it may still be in use by VkSwapchainKHR 0x1150000000115.
Most recently acquired image indices: [0], 1.
(Brackets mark the last use of VkSemaphore 0x7b000000007b in a presentation operation.)
Swapchain image 0 was presented but was not re-acquired, so VkSemaphore 0x7b000000007b may still be in use and cannot be safely reused with image index 1.
Vulkan insight: See https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html for details on swapchain semaphore reuse. Examples of possible approaches:
   a) Use a separate semaphore per swapchain image. Index these semaphores using the index of the acquired image.
   b) Consider the VK_KHR_swapchain_maintenance1 extension. It allows using a VkFence with the presentation operation.
The Vulkan spec states: Each binary semaphore element of the pSignalSemaphores member of any element of pSubmits must be unsignaled when the semaphore signal operation it defines is executed on the device (https://docs.vulkan.org/spec/latest/chapters/cmdbuffers.html#VUID-vkQueueSubmit-pSignalSemaphores-00067)
Objects: 2
    [0] VkSemaphore 0x7b000000007b
    [1] VkQueue 0x193ee99e560
