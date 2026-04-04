#ifndef TXN_DESCRIBE_H
#define TXN_DESCRIBE_H

// Convenience macro to generate txn_describe() for aggregate structs.
// Usage: TXN_DESCRIBE(MyStruct, field1, field2, field3)
//
// Alternatively, write txn_describe() manually:
//   auto txn_describe(MyStruct*) {
//       return txn::describe<MyStruct>(
//           txn::field(&MyStruct::field1, "field1"),
//           txn::field(&MyStruct::field2, "field2")
//       );
//   }

#define TXN_CONCAT_(a, b) a##b
#define TXN_CONCAT(a, b) TXN_CONCAT_(a, b)

#define TXN_FIELD(Type, name) txn::field(&Type::name, #name)

#define TXN_FE_1(T, x) TXN_FIELD(T, x)
#define TXN_FE_2(T, x, ...) TXN_FIELD(T, x), TXN_FE_1(T, __VA_ARGS__)
#define TXN_FE_3(T, x, ...) TXN_FIELD(T, x), TXN_FE_2(T, __VA_ARGS__)
#define TXN_FE_4(T, x, ...) TXN_FIELD(T, x), TXN_FE_3(T, __VA_ARGS__)
#define TXN_FE_5(T, x, ...) TXN_FIELD(T, x), TXN_FE_4(T, __VA_ARGS__)
#define TXN_FE_6(T, x, ...) TXN_FIELD(T, x), TXN_FE_5(T, __VA_ARGS__)
#define TXN_FE_7(T, x, ...) TXN_FIELD(T, x), TXN_FE_6(T, __VA_ARGS__)
#define TXN_FE_8(T, x, ...) TXN_FIELD(T, x), TXN_FE_7(T, __VA_ARGS__)
#define TXN_FE_9(T, x, ...) TXN_FIELD(T, x), TXN_FE_8(T, __VA_ARGS__)
#define TXN_FE_10(T, x, ...) TXN_FIELD(T, x), TXN_FE_9(T, __VA_ARGS__)
#define TXN_FE_11(T, x, ...) TXN_FIELD(T, x), TXN_FE_10(T, __VA_ARGS__)
#define TXN_FE_12(T, x, ...) TXN_FIELD(T, x), TXN_FE_11(T, __VA_ARGS__)
#define TXN_FE_13(T, x, ...) TXN_FIELD(T, x), TXN_FE_12(T, __VA_ARGS__)
#define TXN_FE_14(T, x, ...) TXN_FIELD(T, x), TXN_FE_13(T, __VA_ARGS__)
#define TXN_FE_15(T, x, ...) TXN_FIELD(T, x), TXN_FE_14(T, __VA_ARGS__)
#define TXN_FE_16(T, x, ...) TXN_FIELD(T, x), TXN_FE_15(T, __VA_ARGS__)

#define TXN_COUNT_IMPL( \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, N, ...) N
#define TXN_COUNT(...) TXN_COUNT_IMPL(__VA_ARGS__, \
    16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1)

#define TXN_FOR_EACH(T, ...) \
    TXN_CONCAT(TXN_FE_, TXN_COUNT(__VA_ARGS__))(T, __VA_ARGS__)

#define TXN_DESCRIBE(Type, ...) \
    inline auto txn_describe(Type*) { \
        return txn::describe<Type>( \
            TXN_FOR_EACH(Type, __VA_ARGS__) \
        ); \
    }

#endif // TXN_DESCRIBE_H
