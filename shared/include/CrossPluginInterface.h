// CrossPluginInterface.h
// C-style interfaces for cross-plugin communication
// These interfaces work across dylib boundaries without RTTI

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Interface version for compatibility checking
#define CROSS_PLUGIN_INTERFACE_VERSION 1

// Magic number to identify modules that support cross-plugin interface
#define CROSS_PLUGIN_MAGIC 0x43315850  // "C1XP" in hex

// ChanIn VU Levels interface
// Allows external plugins to read VU meter levels from ChanIn
typedef struct {
    int version;
    float (*getVuLevelL)(void* module);
    float (*getVuLevelR)(void* module);
} ChanInVuInterface;

// ChanOut Mode and VU interface
// Allows external plugins to read output mode and VU levels from ChanOut
typedef struct {
    int version;
    int (*getOutputMode)(void* module);
    float (*getVuLevelL)(void* module);
    float (*getVuLevelR)(void* module);
} ChanOutInterface;

// Expander message for cross-plugin interface discovery
// Modules write this to leftExpander.producerMessage
// External modules read from module->leftExpander.producerMessage
typedef struct {
    uint32_t magic;              // Must be CROSS_PLUGIN_MAGIC
    int interfaceType;           // 1 = ChanIn, 2 = ChanOut
    void* interfacePtr;          // Pointer to ChanInVuInterface or ChanOutInterface
} CrossPluginExpanderMessage;

#define CROSS_PLUGIN_INTERFACE_CHANIN  1
#define CROSS_PLUGIN_INTERFACE_CHANOUT 2

#ifdef __cplusplus
}

// Helper function to read interface from a module's leftExpander
// Returns nullptr if module doesn't expose cross-plugin interface
inline void* getCrossPluginInterface(void* moduleLeftExpanderProducerMessage, int expectedType) {
    if (!moduleLeftExpanderProducerMessage) return nullptr;

    CrossPluginExpanderMessage* msg = static_cast<CrossPluginExpanderMessage*>(moduleLeftExpanderProducerMessage);
    if (msg->magic != CROSS_PLUGIN_MAGIC) return nullptr;
    if (msg->interfaceType != expectedType) return nullptr;

    return msg->interfacePtr;
}

inline ChanInVuInterface* getChanInInterfaceFromExpander(void* moduleLeftExpanderProducerMessage) {
    return static_cast<ChanInVuInterface*>(
        getCrossPluginInterface(moduleLeftExpanderProducerMessage, CROSS_PLUGIN_INTERFACE_CHANIN)
    );
}

inline ChanOutInterface* getChanOutInterfaceFromExpander(void* moduleLeftExpanderProducerMessage) {
    return static_cast<ChanOutInterface*>(
        getCrossPluginInterface(moduleLeftExpanderProducerMessage, CROSS_PLUGIN_INTERFACE_CHANOUT)
    );
}

#endif
