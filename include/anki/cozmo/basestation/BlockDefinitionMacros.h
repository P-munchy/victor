
#ifndef BLOCK_DEFINITION_MODE

#define BLOCK_ENUM_MODE                0
#define BLOCK_ENUM_VALUE_MODE          1
#define BLOCK_LUT_MODE                 2
#define BLOCK_TYPE_TO_STRING_LUT_MODE  3
#define BLOCK_STRING_TO_TYPE_LUT_MODE  4

#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__)
#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__)
#define ADD_ALL_FACES(__SIZE__, __CODE__)
#define END_BLOCK_DEFINITION

#else

#undef START_BLOCK_DEFINITION
#undef ADD_FACE_CODE
#undef ADD_ALL_FACES
#undef END_BLOCK_DEFINITION

//
// Block ID Enumeration Mode
//
#if BLOCK_DEFINITION_MODE == BLOCK_ENUM_MODE

//#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) __NAME__##_BLOCK_TYPE,
#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) static const Type __NAME__;
#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__)
#define END_BLOCK_DEFINITION

#elif BLOCK_DEFINITION_MODE == BLOCK_ENUM_VALUE_MODE

//#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) __NAME__##_BLOCK_TYPE,
#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) const Block::Type Block::Type::__NAME__;
#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__)
#define END_BLOCK_DEFINITION

//
// Block Property LUT Mode
//
#elif BLOCK_DEFINITION_MODE == BLOCK_LUT_MODE

#define UNWRAP(...) __VA_ARGS__
#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) \
{Block::Type::__NAME__, {.name = QUOTE(__NAME__), .color = __COLOR__, .size = {UNWRAP __SIZE__}, .faces = {

#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__) \
{.whichFace = __WHICHFACE__, .size = __SIZE__, .code = __CODE__},

#define END_BLOCK_DEFINITION  } } },

//
// Block ID to String LUT Mode
//
#elif BLOCK_DEFINITION_MODE == BLOCK_TYPE_TO_STRING_LUT_MODE

#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) {Block::Type::__NAME__, QUOTE(__NAME__)},
#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__)
#define END_BLOCK_DEFINITION

//
// Block String Name to Type LUT Mode
//
#elif BLOCK_DEFINITION_MODE == BLOCK_STRING_TO_TYPE_LUT_MODE

#define START_BLOCK_DEFINITION(__NAME__, __SIZE__, __COLOR__) {QUOTE(__NAME__), Block::Type::__NAME__},
#define ADD_FACE_CODE(__WHICHFACE__, __SIZE__, __CODE__)
#define END_BLOCK_DEFINITION

//
// Unknown mode! (Error)
//
#else
#error Unknown BLOCK_DEFINITION_MODE!

#endif // #if/elif BLOCK_DEFINITION_MODE == <which_mode>

#define ADD_ALL_FACES(__SIZE__, __CODE__) \
ADD_FACE_CODE(FRONT_FACE,  __SIZE__, __CODE__) \
ADD_FACE_CODE(BACK_FACE,   __SIZE__, __CODE__) \
ADD_FACE_CODE(LEFT_FACE,   __SIZE__, __CODE__) \
ADD_FACE_CODE(RIGHT_FACE,  __SIZE__, __CODE__) \
ADD_FACE_CODE(TOP_FACE,    __SIZE__, __CODE__) \
ADD_FACE_CODE(BOTTOM_FACE, __SIZE__, __CODE__)

#undef BLOCK_DEFINITION_MODE

#endif // #ifndef BLOCK_DEFINTION_MODE
