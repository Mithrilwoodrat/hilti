#include <Func.h>
#include <Type.h>
#undef List

#include <hilti/hilti.h>
#include <spicy/spicy.h>
#include <util/util.h>

#include "Compiler.h"
#include "ConversionBuilder.h"
#include "ModuleBuilder.h"

using namespace bro::hilti::compiler;

ConversionBuilder::ConversionBuilder(class ModuleBuilder* mbuilder) : BuilderBase(mbuilder)
{
}

shared_ptr<::hilti::Expression> ConversionBuilder::ConvertBroToHilti(
    shared_ptr<::hilti::Expression> val, const ::BroType* type, bool constant)
{
    _constant = constant;

    switch ( type->Tag() ) {
    case TYPE_ADDR:
    case TYPE_ANY:
    case TYPE_BOOL:
    case TYPE_COUNT:
    case TYPE_COUNTER:
    case TYPE_DOUBLE:
    case TYPE_FILE:
    case TYPE_FUNC:
    case TYPE_INT:
    case TYPE_INTERVAL:
    case TYPE_PATTERN:
    case TYPE_PORT:
    case TYPE_STRING:
    case TYPE_TIME:
    case TYPE_TYPE:
        return BroToHiltiBaseType(val, static_cast<const ::BroType*>(type));

    case TYPE_ENUM:
        return BroToHilti(val, static_cast<const ::EnumType*>(type));

    case TYPE_LIST:
        return BroToHilti(val, static_cast<const ::TypeList*>(type));

    case TYPE_OPAQUE:
        return BroToHilti(val, static_cast<const ::OpaqueType*>(type));

    case TYPE_RECORD:
        return BroToHilti(val, static_cast<const ::RecordType*>(type));

    case TYPE_SUBNET:
        return BroToHilti(val, static_cast<const ::SubNetType*>(type));

    case TYPE_TABLE:
        return BroToHilti(val, static_cast<const ::TableType*>(type));

    case TYPE_VECTOR:
        return BroToHilti(val, static_cast<const ::VectorType*>(type));

    default:
        Error(::util::fmt("ConversionBuilder/B2H: unsupported value of type %s",
                          ::type_name(type->Tag())));
    }

    // Cannot be reached.
    return nullptr;
}

shared_ptr<::hilti::Expression> ConversionBuilder::ConvertHiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::BroType* type,
    shared_ptr<::spicy::Type> spicy_type)
{
    _spicy_type = spicy_type;

    switch ( type->Tag() ) {
    case TYPE_ADDR:
    case TYPE_ANY:
    case TYPE_BOOL:
    case TYPE_COUNT:
    case TYPE_COUNTER:
    case TYPE_DOUBLE:
    case TYPE_FILE:
    case TYPE_FUNC:
    case TYPE_INT:
    case TYPE_INTERVAL:
    case TYPE_PATTERN:
    case TYPE_PORT:
    case TYPE_STRING:
    case TYPE_TIME:
    case TYPE_TYPE:
    case TYPE_VOID:
        return HiltiToBroBaseType(val, static_cast<const ::BroType*>(type));

    case TYPE_ENUM:
        return HiltiToBro(val, static_cast<const ::EnumType*>(type));

    case TYPE_LIST:
        return HiltiToBro(val, static_cast<const ::TypeList*>(type));

    case TYPE_OPAQUE:
        return HiltiToBro(val, static_cast<const ::OpaqueType*>(type));

    case TYPE_RECORD:
        return HiltiToBro(val, static_cast<const ::RecordType*>(type));

    case TYPE_SUBNET:
        return HiltiToBro(val, static_cast<const ::SubNetType*>(type));

    case TYPE_TABLE:
        return HiltiToBro(val, static_cast<const ::TableType*>(type));

    case TYPE_VECTOR:
        return HiltiToBro(val, static_cast<const ::VectorType*>(type));

    default:
        Error(::util::fmt("ConversionBuilder/H2B: unsupported value of type %s",
                          ::type_name(type->Tag())));
    }

    // Cannot be reached.
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroInternalInt(
    std::shared_ptr<::hilti::Expression> val)
{
    auto i = Builder()->addTmp("i", ::hilti::builder::integer::type(64));
    Builder()->addInstruction(i, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_internal_int"),
                              ::hilti::builder::tuple::create({val}));
    return i;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroInternalDouble(
    std::shared_ptr<::hilti::Expression> val)
{
    auto d = Builder()->addTmp("d", ::hilti::builder::double_::type());
    Builder()->addInstruction(d, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_internal_double"),
                              ::hilti::builder::tuple::create({val}));
    return d;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BuildConversionFunction(
    const char* tag, shared_ptr<::hilti::Expression> val, shared_ptr<::hilti::Type> valtype,
    shared_ptr<::hilti::Type> dsttype, const ::BroType* type, build_conversion_function_callback cb)
{
    auto mbuilder = ModuleBuilder();
    auto glue_mbuilder = GlueModuleBuilder();
    auto glue_ns = glue_mbuilder->module()->id()->name();

    auto fname = ::util::fmt("%s_%s", tag, HiltiSymbol(type));
    auto qualified_fname = ::util::fmt("%s::%s_%s", glue_ns, tag, HiltiSymbol(type));

    if ( ! glue_mbuilder->declared(fname) ) {
        Compiler()->pushModuleBuilder(glue_mbuilder);

        // Note, we need to compute these type here separately from
        // those below. By calling HiltiType() only after the glue
        // builder has been pushed, we make sure that they reference
        // external types correctly.
        auto gvaltype = valtype ? valtype : HiltiType(type);
        auto gdsttype = dsttype ? dsttype : HiltiType(type);

        auto result = ::hilti::builder::function::result(gdsttype);
        auto param = ::hilti::builder::function::parameter("val", gvaltype, false, nullptr);
        auto func = glue_mbuilder->pushFunction(fname, result, {param});
        glue_mbuilder->exportID(func->id());

        auto dst = cb(::hilti::builder::id::create("val"), type);
        Builder()->addInstruction(::hilti::instruction::flow::ReturnResult, dst);

        glue_mbuilder->popFunction();

        Compiler()->popModuleBuilder();
    }

    if ( ! valtype )
        valtype = HiltiType(type);

    if ( ! dsttype )
        dsttype = HiltiType(type);

    if ( ! mbuilder->declared(qualified_fname) ) {
        auto result = ::hilti::builder::function::result(dsttype);
        auto param = ::hilti::builder::function::parameter("val", valtype, false, nullptr);
        mbuilder->declareFunction(qualified_fname, result, {param});
    }

    auto tmp = Builder()->addTmp("conv", dsttype);

    Builder()->addInstruction(tmp, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create(qualified_fname),
                              ::hilti::builder::tuple::create({val}));

    return tmp;
}


void ConversionBuilder::MapType(const ::BroType* from, const ::BroType* to)
{
    if ( from == to )
        return;

#ifdef DEBUG
    ODesc d1, d2;
    d1.SetShort();
    d2.SetShort();
    from->Describe(&d1);
    to->Describe(&d2);

    DBG_LOG_COMPILER("Mapping type %s to %s", d1.Description(), d2.Description());
#endif

    auto glue_mbuilder = GlueModuleBuilder();

    Compiler()->pushModuleBuilder(glue_mbuilder);
    glue_mbuilder->pushModuleInit();

    auto hfrom = CreateBroType(from);
    auto hto = CreateBroType(to);
    Builder()->addInstruction(hfrom, ::hilti::instruction::operator_::Assign, hto);

    glue_mbuilder->popFunction();
    Compiler()->popModuleBuilder();
}

void ConversionBuilder::FinalizeTypes()
{
    return;
}

bool ConversionBuilder::IsShadowedType(const BroType* type) const
{
    if ( type->GetName().empty() )
        return false;

    // When adding a type here, make sure to sets its
    // notify_plugins_on_dtor flag in Bro.

    auto name = ::extract_var_name(type->GetName().c_str());
    return name == "connection" || name == "dns_msg" || name == "dns_answer" || name == "fa_file";
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiGlobalForType(const char* tag,
                                                                           const ::BroType* type)
{
    auto btype = ::hilti::builder::type::byName("LibBro::BroType");

    auto mbuilder = ModuleBuilder();
    auto glue_mbuilder = GlueModuleBuilder();
    auto glue_ns = glue_mbuilder->module()->id()->name();

    auto tname = ::util::fmt("%s_%s", tag, HiltiSymbol(type));
    auto qualified_tname = ::util::fmt("%s::%s_%s", glue_ns, tag, HiltiSymbol(type));

    if ( ! glue_mbuilder->declared(tname) ) {
        auto global = glue_mbuilder->addGlobal(tname, btype);
        glue_mbuilder->exportID(tname);
    }

    if ( ! mbuilder->declared(qualified_tname) )
        mbuilder->declareGlobal(qualified_tname, btype);

    return mbuilder != glue_mbuilder ? ::hilti::builder::id::create(qualified_tname) :
                                       ::hilti::builder::id::create(tname);
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BuildCreateBroType(
    const char* tag, const ::BroType* type, build_create_type_callback cb)
{
    auto id = type->GetName();
    if ( id.size() )
        // We don't need to actually build it, can just look it up.
        return BuildCreateBroTypeInternal(tag, type, [&](shared_ptr<::hilti::Expression> dst,
                                                         const ::BroType* type) {
            auto f = ::hilti::builder::id::create("LibBro::bro_lookup_type");
            auto args = ::hilti::builder::tuple::create({::hilti::builder::string::create(id)});
            Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult, f, args);
        });
    else
        // Need to build it ourselves.
        return BuildCreateBroTypeInternal(tag, type, cb);
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BuildCreateBroTypeInternal(
    const char* tag, const ::BroType* type, build_create_type_callback cb)
{
    auto btype = ::hilti::builder::type::byName("LibBro::BroType");

    auto mbuilder = ModuleBuilder();
    auto glue_mbuilder = GlueModuleBuilder();
    auto glue_ns = glue_mbuilder->module()->id()->name();

    auto tname = ::util::fmt("%s_%s", tag, HiltiSymbol(type));
    auto qualified_tname = ::util::fmt("%s::%s_%s", glue_ns, tag, HiltiSymbol(type));

    if ( ! glue_mbuilder->declared(tname) ) {
        auto global = glue_mbuilder->addGlobal(tname, btype);
        glue_mbuilder->exportID(tname);

        Compiler()->pushModuleBuilder(glue_mbuilder);
        glue_mbuilder->pushModuleInit();

        cb(global, type);

        glue_mbuilder->popFunction();
        Compiler()->popModuleBuilder();
    }

    if ( ! mbuilder->declared(qualified_tname) )
        mbuilder->declareGlobal(qualified_tname, btype);

    auto t = (mbuilder != glue_mbuilder ? ::hilti::builder::id::create(qualified_tname) :
                                          ::hilti::builder::id::create(tname));

    BroRef(t);
    return t;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::PostponeBuildCreateBroType(
    const char* tag, const ::BroType* type, build_create_type_callback cb)
{
    if ( type->GetName().size() )
        // As we can just look these up, we don't need to postpone.
        return BuildCreateBroType(tag, type, cb);

    postponed_types.insert(std::make_pair(type, std::make_pair(string(tag), cb)));

    auto t = HiltiGlobalForType(tag, type);
    BroRef(t);
    return t;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHiltiBaseType(
    shared_ptr<::hilti::Expression> val, const ::BroType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("b2h", HiltiType(type));

    switch ( type->Tag() ) {
    case TYPE_ADDR: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_address"), args);
        return dst;
    }

    case TYPE_ANY: {
        auto ty_bany =
            ::hilti::builder::reference::type(::hilti::builder::type::byName("LibBro::BroAny"));
        Builder()->addInstruction(dst, ::hilti::instruction::operator_::Assign,
                                  ::hilti::builder::expression::default_(ty_bany));
        return dst;
    }

    case TYPE_BOOL: {
        Builder()->addInstruction(dst, ::hilti::instruction::operator_::Unequal,
                                  BroInternalInt(val), ::hilti::builder::integer::create(0));
        return dst;
    }

    case TYPE_COUNT:
    case TYPE_COUNTER:
        return BroInternalInt(val);

    case TYPE_DOUBLE:
        return BroInternalDouble(val);

    case TYPE_FILE: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_file"), args);
        return dst;
    }

    case TYPE_FUNC: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_function"), args);
        return dst;
    }

    case TYPE_INT:
        return BroInternalInt(val);

    case TYPE_INTERVAL: {
        Builder()->addInstruction(dst, ::hilti::instruction::interval::FromDouble,
                                  BroInternalDouble(val));
        return dst;
    }

    case TYPE_PATTERN: {
        static int cnt = 0;

        auto mbuilder = Builder()->moduleBuilder();
        auto idx = val->render();

        if ( ! _constant ) {
            auto args = ::hilti::builder::tuple::create({val});
            Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create("LibBro::b2h_pattern"), args);
            return dst;
        }

        if ( auto cached = mbuilder->lookupNode("b2h_pattern", idx) )
            return ast::rtti::checkedCast<::hilti::Expression>(cached);

        mbuilder->pushModuleInit();

        auto glob =
            mbuilder->addGlobal(::hilti::builder::id::node(::util::fmt("__b2h_pattern%d", ++cnt)),
                                dst->type());

        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(glob, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_pattern"), args);

        mbuilder->popModuleInit();

        mbuilder->cacheNode("b2h_pattern", idx, glob);
        return glob;
    }

    case TYPE_PORT: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_port"), args);
        return dst;
    }

    case TYPE_STRING: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_string"), args);
        return dst;
    }

    case TYPE_TIME: {
        Builder()->addInstruction(dst, ::hilti::instruction::time::FromDouble,
                                  BroInternalDouble(val));
        return dst;
    }

    case TYPE_TYPE: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::b2h_type"), args);
        return dst;
    }

    default:
        Error(::util::fmt("ConversionBuilder/B2H: unsupported type %s", ::type_name(type->Tag())));
    }

    // Cannot be reached.
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::EnumType* type)
{
    auto dst = Builder()->addTmp("b2h", HiltiType(type));

    Builder()->addInstruction(dst, ::hilti::instruction::enum_::FromInt, BroInternalInt(val));
    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::TypeList* type)
{
    ::hilti::builder::tuple::element_list elems;

    auto types = type->Types();

    for ( int i = 0; i < types->length(); i++ ) {
        auto hidx = ::hilti::builder::integer::create(i);
        auto tmp = Builder()->addTmp("elem", HiltiType((*types)[i]));

        Builder()->addInstruction(tmp, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_list_index"),
                                  ::hilti::builder::tuple::create({val, hidx}));

        elems.push_back(tmp);
    }

    auto dst = Builder()->addTmp("b2h", HiltiType(type));

    Builder()->addInstruction(dst, ::hilti::instruction::operator_::Assign,
                              ::hilti::builder::tuple::create(elems));

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::OpaqueType* type)
{
    Error("ConversionBuilder/B2H: no support yet for converting OpaqueVal");
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::RecordType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");

    auto t = HiltiType(type);
    auto rtype = ast::rtti::checkedCast<type::Reference>(t);
    auto stype = ast::rtti::checkedCast<type::Struct>(rtype->argType());

    auto dst = Builder()->addTmp("b2h", t);

    std::shared_ptr<::hilti::builder::BlockBuilder> cached = nullptr;
    std::shared_ptr<::hilti::builder::BlockBuilder> not_cached = nullptr;
    std::shared_ptr<::hilti::builder::BlockBuilder> done = nullptr;

    if ( IsShadowedType(type) ) {
        // Check runtime object registry first.
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create(
                                      "LibBro::object_mapping_lookup_hilti"),
                                  ::hilti::builder::tuple::create({val}));

        auto b = Builder()->addIfElse(dst);
        cached = std::get<0>(b);
        not_cached = std::get<1>(b);
        done = std::get<2>(b);

        ModuleBuilder()->pushBuilder(not_cached);
    }

    Builder()->addInstruction(dst, ::hilti::instruction::struct_::New,
                              ::hilti::builder::type::create(stype));

    ::hilti::builder::tuple::element_list elems;

    for ( int i = 0; i < type->NumFields(); i++ ) {
        auto hidx = ::hilti::builder::integer::create(i);

        auto tmp = Builder()->addTmp("field", vtype);

        Builder()->addInstruction(tmp, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_record_index"),
                                  ::hilti::builder::tuple::create({val, hidx}));

        auto b = Builder()->addIf(tmp);
        auto is_set = std::get<0>(b);
        auto cont = std::get<1>(b);

        ModuleBuilder()->pushBuilder(is_set);

        auto val = RuntimeValToHilti(tmp, type->FieldType(i));
        BroUnref(tmp);

        Builder()->addInstruction(::hilti::instruction::struct_::Set, dst,
                                  ::hilti::builder::string::create(type->FieldName(i)), val);

        Builder()->addInstruction(::hilti::instruction::flow::Jump, cont->block());

        ModuleBuilder()->popBuilder(is_set);

        ModuleBuilder()->pushBuilder(cont);
    }

    if ( IsShadowedType(type) ) {
        Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                  ::hilti::builder::id::create("LibBro::object_mapping_register"),
                                  ::hilti::builder::tuple::create({val, dst}));

        Builder()->addInstruction(::hilti::instruction::flow::Jump, done->block());

        ModuleBuilder()->popBuilder(not_cached);

        ModuleBuilder()->pushBuilder(cached);
        Builder()->addInstruction(::hilti::instruction::flow::Jump, done->block());
        ModuleBuilder()->popBuilder(cached);

        ModuleBuilder()->pushBuilder(done);
    }

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::SubNetType* type)
{
    auto dst = Builder()->addTmp("b2h", HiltiType(type));

    auto args = ::hilti::builder::tuple::create({val});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::b2h_subnet"), args);

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::TableType* type)
{
    // TODO: Check that we get Bro's reference counting right.
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");

    if ( type->IsSet() ) {
        return BuildConversionFunction(
            "b2h", val, vtype, 0, type,
            [&](shared_ptr<::hilti::Expression> val,
                const ::BroType* type) -> shared_ptr<::hilti::Expression> {
                auto mbuilder = ModuleBuilder();

                auto dsttype = HiltiType(type);
                auto dst = Builder()->addTmp("dst", dsttype);

                auto rtype = ast::rtti::checkedCast<type::Reference>(dsttype);
                auto stype = ast::rtti::checkedCast<type::Set>(rtype->argType());
                auto expr_stype = ::hilti::builder::type::create(stype);
                auto expr_ktype = ::hilti::builder::type::create(stype->elementType());

                Builder()->addInstruction(dst, ::hilti::instruction::set::New, expr_stype);

                auto loop = mbuilder->pushBuilder("loop");

                auto cookie = Builder()->addTmp("cookie", ::hilti::builder::caddr::type());
                auto kval = Builder()->addTmp("k", vtype);

                auto ttype =
                    ::hilti::builder::tuple::type({::hilti::builder::caddr::type(), vtype, vtype});
                auto t = Builder()->addTmp("t", ttype);

                Builder()->addInstruction(t, ::hilti::instruction::flow::CallResult,
                                          ::hilti::builder::id::create("LibBro::bro_table_iterate"),
                                          ::hilti::builder::tuple::create({val, cookie}));

                Builder()->addInstruction(cookie, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(0));
                Builder()->addInstruction(kval, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(1));

                auto blocks = Builder()->addIf(kval);
                auto cont = std::get<0>(blocks);
                auto done = std::get<1>(blocks);

                mbuilder->popBuilder(loop);

                mbuilder->pushBuilder(cont);

                auto itypes = type->AsTableType()->Indices();
                const ::BroType* index_type = itypes;

                if ( itypes->Types()->length() == 1 )
                    index_type = (*itypes->Types())[0];

                auto k = RuntimeValToHilti(kval, index_type);
                BroUnref(kval);

                Builder()->addInstruction(::hilti::instruction::set::Insert, dst, k);
                Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

                mbuilder->popBuilder(cont);

                mbuilder->pushBuilder(done);

                return dst;
            });
    }

    else // A table, not a set.
    {
        return BuildConversionFunction(
            "b2h", val, vtype, 0, type,
            [&](shared_ptr<::hilti::Expression> val,
                const ::BroType* type) -> shared_ptr<::hilti::Expression> {
                auto mbuilder = ModuleBuilder();

                auto dsttype = HiltiType(type);
                auto dst = Builder()->addTmp("dst", dsttype);

                auto rtype = ast::rtti::checkedCast<type::Reference>(dsttype);
                auto mtype = ast::rtti::checkedCast<type::Map>(rtype->argType());
                auto expr_mtype = ::hilti::builder::type::create(mtype);
                auto expr_ktype = ::hilti::builder::type::create(mtype->keyType());
                auto expr_vtype = ::hilti::builder::type::create(mtype->valueType());

                Builder()->addInstruction(dst, ::hilti::instruction::map::New, expr_mtype);

                auto loop = mbuilder->pushBuilder("loop");

                auto cookie = Builder()->addTmp("cookie", ::hilti::builder::caddr::type());
                auto kval = Builder()->addTmp("k", vtype);
                auto vval = Builder()->addTmp("v", vtype);

                auto ttype =
                    ::hilti::builder::tuple::type({::hilti::builder::caddr::type(), vtype, vtype});
                auto t = Builder()->addTmp("t", ttype);

                Builder()->addInstruction(t, ::hilti::instruction::flow::CallResult,
                                          ::hilti::builder::id::create("LibBro::bro_table_iterate"),
                                          ::hilti::builder::tuple::create({val, cookie}));

                Builder()->addInstruction(cookie, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(0));
                Builder()->addInstruction(kval, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(1));
                Builder()->addInstruction(vval, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(2));

                auto blocks = Builder()->addIf(kval);
                auto cont = std::get<0>(blocks);
                auto done = std::get<1>(blocks);

                mbuilder->popBuilder(loop);

                mbuilder->pushBuilder(cont);

                auto itypes = type->AsTableType()->Indices();
                const ::BroType* index_type = itypes;

                if ( itypes->Types()->length() == 1 )
                    index_type = (*itypes->Types())[0];

                auto k = RuntimeValToHilti(kval, index_type);
                auto v = RuntimeValToHilti(vval, type->AsTableType()->YieldType());

                BroUnref(kval);
                BroUnref(vval);

                Builder()->addInstruction(::hilti::instruction::map::Insert, dst, k, v);
                Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

                mbuilder->popBuilder(cont);

                mbuilder->pushBuilder(done);

                return dst;
            });
    }
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::BroToHilti(
    shared_ptr<::hilti::Expression> val, const ::VectorType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");

    return BuildConversionFunction(
        "b2h", val, vtype, 0, type, [&](shared_ptr<::hilti::Expression> val,
                                        const ::BroType* type) -> shared_ptr<::hilti::Expression> {
            auto mbuilder = ModuleBuilder();

            auto dsttype = HiltiType(type);
            auto dst = Builder()->addTmp("dst", dsttype);

            auto rtype = ast::rtti::checkedCast<type::Reference>(dsttype);
            auto vectype = ast::rtti::checkedCast<type::Vector>(rtype->argType());
            auto expr_vectype = ::hilti::builder::type::create(vectype);

            Builder()->addInstruction(dst, ::hilti::instruction::vector::New, expr_vectype);

            auto finished = mbuilder->newBuilder("finished");
            auto loop = mbuilder->pushBuilder("loop");

            auto cookie = Builder()->addTmp("cookie", ::hilti::builder::caddr::type());
            auto eval = Builder()->addTmp("e", vtype);

            auto ttype = ::hilti::builder::tuple::type({::hilti::builder::caddr::type(), vtype});
            auto t = Builder()->addTmp("t", ttype);

            Builder()->addInstruction(t, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create("LibBro::bro_vector_iterate"),
                                      ::hilti::builder::tuple::create({val, cookie}));

            Builder()->addInstruction(cookie, ::hilti::instruction::tuple::Index, t,
                                      ::hilti::builder::integer::create(0));

            auto next = mbuilder->newBuilder("next");

            Builder()->addInstruction(::hilti::instruction::flow::IfElse, cookie, next->block(),
                                      finished->block());

            mbuilder->popBuilder(loop);

            mbuilder->pushBuilder(next);

            Builder()->addInstruction(eval, ::hilti::instruction::tuple::Index, t,
                                      ::hilti::builder::integer::create(1));

            auto blocks = Builder()->addIfElse(eval);
            auto have = std::get<0>(blocks);
            auto have_not = std::get<1>(blocks);
            auto done = std::get<2>(blocks);

            mbuilder->popBuilder(next);

            mbuilder->pushBuilder(have);

            auto e = RuntimeValToHilti(eval, type->YieldType());
            BroUnref(eval);

            Builder()->addInstruction(::hilti::instruction::vector::PushBack, dst, e);
            Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

            mbuilder->popBuilder(have);

            mbuilder->pushBuilder(have_not);

            e = builder::expression::default_(HiltiType(type->YieldType()));
            Builder()->addInstruction(::hilti::instruction::vector::PushBack, dst, e);
            Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

            mbuilder->popBuilder(have_not);

            mbuilder->pushBuilder(finished);

            return dst;
        });
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBroBaseType(
    shared_ptr<::hilti::Expression> val, const ::BroType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    switch ( type->Tag() ) {
    case TYPE_ADDR: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_address"), args);
        return dst;
    }

    case TYPE_ANY: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::any_to_val"), args);
        return dst;
    }

    case TYPE_BOOL: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_bool"), args);
        return dst;
    }

    case TYPE_COUNT:
    case TYPE_COUNTER: {
        auto ext = Builder()->addTmp("ext", ::hilti::builder::integer::type(64));
        Builder()->addInstruction(ext, ::hilti::instruction::integer::ZExt, val);
        auto args = ::hilti::builder::tuple::create({ext});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_integer_unsigned"),
                                  args);
        return dst;
    }

    case TYPE_DOUBLE: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_double"), args);
        return dst;
    }

    case TYPE_FILE: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_file"), args);
        return dst;
    }

    case TYPE_FUNC: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_function"), args);
        return dst;
    }

    case TYPE_INT: {
        auto ext = Builder()->addTmp("ext", ::hilti::builder::integer::type(64));
        Builder()->addInstruction(ext, ::hilti::instruction::integer::SExt, val);
        auto args = ::hilti::builder::tuple::create({ext});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_integer_signed"), args);
        return dst;
    }

    case TYPE_INTERVAL: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_interval"), args);
        return dst;
    }

    case TYPE_PATTERN: {
        static int cnt = 0;

        auto mbuilder = Builder()->moduleBuilder();
        auto idx = val->render();

        auto is_const = ast::rtti::isA<expression::Ctor>(val);

        if ( ! is_const ) {
            auto args = ::hilti::builder::tuple::create({val});
            Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create("LibBro::h2b_regexp"), args);
            return dst;
        }

        if ( auto cached = mbuilder->lookupNode("h2b_regexp", idx) ) {
            auto obj = ast::rtti::checkedCast<::hilti::Expression>(cached);
            auto args = ::hilti::builder::tuple::create({obj});
            Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                      ::hilti::builder::id::create("LibBro::object_ref"), args);
            return obj;
        }

        auto init = mbuilder->pushModuleInit();

        auto glob =
            mbuilder->addGlobal(::hilti::builder::id::node(::util::fmt("__h2b_regexp%d", ++cnt)),
                                vtype);
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(glob, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_regexp"), args);

        mbuilder->popModuleInit();

        auto args2 = ::hilti::builder::tuple::create({glob});
        Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                  ::hilti::builder::id::create("LibBro::object_ref"), args2);

        mbuilder->cacheNode("h2b_regexp", idx, glob);
        return glob;
    }

    case TYPE_PORT: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_port"), args);
        return dst;
    }

    case TYPE_STRING: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_bytes"), args);
        return dst;
    }

    case TYPE_TIME: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_time"), args);
        return dst;
    }

    case TYPE_TYPE: {
        auto args = ::hilti::builder::tuple::create({val});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_type"), args);
        return dst;
    }

    case TYPE_VOID: {
        auto args = ::hilti::builder::tuple::create({});
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::h2b_void"), args);
        return dst;
    }

    default:
        Error(::util::fmt("ConversionBuilder/H2B: unsupported type %s (%s)",
                          ::type_name(type->Tag()), val->render()));
    }

    // Cannot be reached.
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::EnumType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    shared_ptr<::hilti::Expression> etype;

    auto cached = Compiler()->LookupCachedCustomBroType(type);

    if ( cached.first ) {
        auto btype = ::hilti::builder::type::byName("LibBro::BroType");
        auto f = ::hilti::builder::id::create("LibBro::bro_lookup_type");
        auto args =
            ::hilti::builder::tuple::create({::hilti::builder::string::create(cached.second)});

        etype = Builder()->addTmp("et", btype);
        Builder()->addInstruction(etype, ::hilti::instruction::flow::CallResult, f, args);
    }

    else
        etype = CreateBroType(type);

    auto args = ::hilti::builder::tuple::create({val, etype});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::h2b_enum_type"), args);
    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::TypeList* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    auto args = ::hilti::builder::tuple::create({});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_list_new"), args);

    auto types = type->Types();

    for ( int i = 0; i < types->length(); i++ ) {
        auto hidx = ::hilti::builder::integer::create(i);
        auto tmp = Builder()->addTmp("elem", HiltiType((*types)[i]));

        Builder()->addInstruction(tmp, ::hilti::instruction::tuple::Index, hidx);

        Builder()->addInstruction(tmp, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_list_append"),
                                  ::hilti::builder::tuple::create({val, tmp}));
    }

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::OpaqueType* type)
{
    Error("ConversionBuilder/H2B: no support yet for converting OpaqueVal");
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::RecordType* type)
{
    if ( ::ast::rtti::tryCast<::hilti::type::Tuple>(val->type()) )
        return HiltiToBroFromTuple(val, type);

    // Note, val may have a type of Unknown here, which we assume to eventually resolve into a
    // struct type.
    return HiltiToBroFromStruct(val, type);
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBroFromTuple(
    shared_ptr<::hilti::Expression> val, const ::RecordType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    auto rtype = CreateBroType(type);
    auto args = ::hilti::builder::tuple::create({rtype});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_record_new"), args);

    auto ttype = ast::rtti::checkedCast<::hilti::type::Tuple>(val->type());
    auto htypes = ttype->typeList();
    auto htype_iter = htypes.begin();

    for ( int i = 0; i < type->NumFields(); i++ ) {
        auto hidx = ::hilti::builder::integer::create(i);
        auto htype = *htype_iter++;

        auto tmp = Builder()->addTmp("ufield", htype);

        Builder()->addInstruction(tmp, ::hilti::instruction::tuple::Index, val, hidx);

        if ( auto u = ast::rtti::tryCast<::hilti::type::Union>(htype) ) {
            // We support unions of a single field here, as used by
            // Spicy's optional type. If the field is set, we just convert
            // it; if not we leave the record field unset (which however must
            // be declared optional).

            if ( ! type->FieldDecl(i)->FindAttr(ATTR_OPTIONAL) ) {
                Error(
                    ::util::fmt("optional tuple element %d used with non-optional record field %s ",
                                i, type->FieldName(i)));
                continue;
            }

            auto fields = u->fields();
            assert(fields.size() == 1);

            auto b = Builder()->addIf(tmp);
            auto set = std::get<0>(b);
            auto cont = std::get<1>(b);

            ModuleBuilder()->pushBuilder(set);
            auto vtmp = Builder()->addTmp("uval", fields.front()->type());
            Builder()->addInstruction(vtmp, ::hilti::instruction::union_::GetType, tmp);

            auto tmp_h2b = RuntimeHiltiToVal(vtmp, type->FieldType(i)); // XXX

            Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                      ::hilti::builder::id::create("LibBro::bro_record_assign"),
                                      ::hilti::builder::tuple::create({dst, hidx, tmp_h2b}));

            BroUnref(tmp_h2b);

            Builder()->addInstruction(::hilti::instruction::flow::Jump, cont->block());
            ModuleBuilder()->popBuilder();

            ModuleBuilder()->pushBuilder(cont);
        }

        else {
            // Standard case.
            auto tmp = Builder()->addTmp("field", htype);

            Builder()->addInstruction(tmp, ::hilti::instruction::tuple::Index, val, hidx);

            auto tmp_h2b = RuntimeHiltiToVal(tmp, type->FieldType(i));

            Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                      ::hilti::builder::id::create("LibBro::bro_record_assign"),
                                      ::hilti::builder::tuple::create({dst, hidx, tmp_h2b}));

            BroUnref(tmp_h2b);
        }
    }

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBroFromStruct(
    shared_ptr<::hilti::Expression> val, const ::RecordType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    std::shared_ptr<::hilti::builder::BlockBuilder> cached = nullptr;
    std::shared_ptr<::hilti::builder::BlockBuilder> not_cached = nullptr;
    std::shared_ptr<::hilti::builder::BlockBuilder> done = nullptr;

    if ( IsShadowedType(type) ) {
        // Check runtime object registry first.
        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::object_mapping_lookup_bro"),
                                  ::hilti::builder::tuple::create({val}));

        auto b = Builder()->addIfElse(dst);
        cached = std::get<0>(b);
        not_cached = std::get<1>(b);
        done = std::get<2>(b);

        ModuleBuilder()->pushBuilder(not_cached);
    }

    auto rtype = CreateBroType(type);
    auto args = ::hilti::builder::tuple::create({rtype});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_record_new"), args);

    bool no_names = false;
    ::hilti::type::Struct::field_list sfields;

    // Can be either ref<struct> or still unknown here.
    if ( auto rstype = ast::rtti::tryCast<::hilti::type::Reference>(val->type()) ) {
        auto sfields = ast::rtti::checkedCast<::hilti::type::Struct>(rstype->argType())->fields();
        auto utype = ast::rtti::tryCast<::spicy::type::Unit>(_spicy_type);
        no_names = (utype && utype->ctorNoNames());
    }

    auto sf = sfields.begin();

    for ( int i = 0; i < type->NumFields(); i++ ) {
        auto hidx = ::hilti::builder::integer::create(i);
        auto field = no_names ? ::hilti::builder::string::create((*sf)->id()->name()) :
                                ::hilti::builder::string::create(type->FieldName(i));

        ++sf;

        auto is_set = Builder()->addTmp("is_set", ::hilti::builder::boolean::type());

        Builder()->addInstruction(is_set, ::hilti::instruction::struct_::IsSet, val, field);

        auto b = Builder()->addIf(is_set);
        auto bis_set = std::get<0>(b);
        auto bcont = std::get<1>(b);

        ModuleBuilder()->pushBuilder(bis_set);

        auto tmp = Builder()->addTmp("field", HiltiType(type->FieldType(i)));

        Builder()->addInstruction(tmp, ::hilti::instruction::struct_::Get, val, field);

        auto tmp_h2b = RuntimeHiltiToVal(tmp, type->FieldType(i));

        Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                  ::hilti::builder::id::create("LibBro::bro_record_assign"),
                                  ::hilti::builder::tuple::create({dst, hidx, tmp_h2b}));

        BroUnref(tmp_h2b);

        Builder()->addInstruction(::hilti::instruction::flow::Jump, bcont->block());

        ModuleBuilder()->popBuilder(bis_set);

        ModuleBuilder()->pushBuilder(bcont);
    }

    if ( IsShadowedType(type) ) {
        // We don't register it this way, only when going from Val to HILTI.
        Builder()->addInstruction(::hilti::instruction::flow::Jump, done->block());

        ModuleBuilder()->popBuilder(not_cached);

        ModuleBuilder()->pushBuilder(cached);
        Builder()->addInstruction(::hilti::instruction::flow::Jump, done->block());
        ModuleBuilder()->popBuilder(cached);

        ModuleBuilder()->pushBuilder(done);
    }

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::SubNetType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    auto args = ::hilti::builder::tuple::create({val});
    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::h2b_network"), args);
    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::TableType* type)
{
    // TODO: Check that we get Bro's reference counting right.

    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");

    if ( type->IsSet() ) {
        return BuildConversionFunction(
            "h2b", val, 0, vtype, type,
            [&](shared_ptr<::hilti::Expression> val,
                const ::BroType* type) -> shared_ptr<::hilti::Expression> {
                auto mbuilder = ModuleBuilder();

                auto dsttype = vtype;
                auto dst = Builder()->addTmp("dst", dsttype);

                auto rtype = ast::rtti::checkedCast<type::Reference>(HiltiType(type));
                auto stype = ast::rtti::checkedCast<type::Set>(rtype->argType());

                auto cur = mbuilder->addTmp("cur", stype->iterType());
                auto end = mbuilder->addTmp("end", stype->iterType());
                auto k = mbuilder->addTmp("k", stype->elementType());
                auto is_end = mbuilder->addTmp("is_end", ::hilti::builder::boolean::type());

                Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                          ::hilti::builder::id::create("LibBro::bro_table_new"),
                                          ::hilti::builder::tuple::create({CreateBroType(type)}));

                Builder()->addInstruction(cur, ::hilti::instruction::operator_::Begin, val);
                Builder()->addInstruction(end, ::hilti::instruction::operator_::End, val);

                auto loop = mbuilder->pushBuilder("loop");

                Builder()->addInstruction(is_end, ::hilti::instruction::operator_::Equal, cur, end);

                auto blocks = Builder()->addIf(is_end);
                auto done = std::get<0>(blocks);
                auto cont = std::get<1>(blocks);

                mbuilder->popBuilder(loop);

                mbuilder->pushBuilder(cont);

                Builder()->addInstruction(k, ::hilti::instruction::operator_::Deref, cur);

                auto itypes = type->AsTableType()->Indices();
                const ::BroType* index_type = itypes;

                if ( itypes->Types()->length() == 1 )
                    index_type = (*itypes->Types())[0];

                auto kval = RuntimeHiltiToVal(k, index_type);

                auto null = ::hilti::builder::caddr::create();

                Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                          ::hilti::builder::id::create("LibBro::bro_table_insert"),
                                          ::hilti::builder::tuple::create({dst, kval, null}));

                BroUnref(kval);

                Builder()->addInstruction(cur, ::hilti::instruction::operator_::Incr, cur);

                Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

                mbuilder->popBuilder(cont);

                mbuilder->pushBuilder(done);

                return dst;
            });
    }

    else // A table, not a set.
    {
        return BuildConversionFunction(
            "h2b", val, 0, vtype, type,
            [&](shared_ptr<::hilti::Expression> val,
                const ::BroType* type) -> shared_ptr<::hilti::Expression> {
                auto mbuilder = ModuleBuilder();

                auto dsttype = vtype;
                auto dst = Builder()->addTmp("dst", dsttype);

                auto rtype = ast::rtti::checkedCast<type::Reference>(HiltiType(type));
                auto mtype = ast::rtti::checkedCast<type::Map>(rtype->argType());

                auto cur = mbuilder->addTmp("cur", mtype->iterType());
                auto end = mbuilder->addTmp("end", mtype->iterType());
                auto t = mbuilder->addTmp("t", mtype->elementType());
                auto is_end = mbuilder->addTmp("is_end", ::hilti::builder::boolean::type());

                Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                          ::hilti::builder::id::create("LibBro::bro_table_new"),
                                          ::hilti::builder::tuple::create({CreateBroType(type)}));

                Builder()->addInstruction(cur, ::hilti::instruction::operator_::Begin, val);
                Builder()->addInstruction(end, ::hilti::instruction::operator_::End, val);

                auto loop = mbuilder->pushBuilder("loop");

                Builder()->addInstruction(is_end, ::hilti::instruction::operator_::Equal, cur, end);

                auto blocks = Builder()->addIf(is_end);
                auto done = std::get<0>(blocks);
                auto cont = std::get<1>(blocks);

                mbuilder->popBuilder(loop);

                mbuilder->pushBuilder(cont);

                Builder()->addInstruction(t, ::hilti::instruction::operator_::Deref, cur);

                auto k = mbuilder->addTmp("k", mtype->keyType());
                auto v = mbuilder->addTmp("v", mtype->valueType());

                Builder()->addInstruction(k, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(0));
                Builder()->addInstruction(v, ::hilti::instruction::tuple::Index, t,
                                          ::hilti::builder::integer::create(1));

                auto itypes = type->AsTableType()->Indices();
                const ::BroType* index_type = itypes;

                if ( itypes->Types()->length() == 1 )
                    index_type = (*itypes->Types())[0];

                auto kval = RuntimeHiltiToVal(k, index_type);
                auto vval = RuntimeHiltiToVal(v, type->AsTableType()->YieldType());

                Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                          ::hilti::builder::id::create("LibBro::bro_table_insert"),
                                          ::hilti::builder::tuple::create({dst, kval, vval}));

                BroUnref(kval);
                BroUnref(vval);

                Builder()->addInstruction(cur, ::hilti::instruction::operator_::Incr, cur);

                Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

                mbuilder->popBuilder(cont);

                mbuilder->pushBuilder(done);

                return dst;
            });
    }
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::HiltiToBro(
    shared_ptr<::hilti::Expression> val, const ::VectorType* type)
{
    auto vtype = ::hilti::builder::type::byName("LibBro::BroVal");
    auto dst = Builder()->addTmp("val", vtype);

    auto mbuilder = ModuleBuilder();
    // auto rvtype = ast::rtti::checkedCast<type::Reference>(HiltiType(type));
    // auto itype = ast::rtti::checkedCast<type::Vector>(rtype->argType());
    auto rtype = ast::rtti::checkedCast<::hilti::type::Reference>(val->type());
    auto itype = ::ast::type::checkedTrait<::hilti::type::trait::Iterable>(rtype->argType());

    auto cur = mbuilder->addTmp("cur", itype->iterType());
    auto end = mbuilder->addTmp("end", itype->iterType());
    auto e = mbuilder->addTmp("e", itype->elementType());
    auto is_end = mbuilder->addTmp("is_end", ::hilti::builder::boolean::type());

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_vector_new"),
                              ::hilti::builder::tuple::create({CreateBroType(type)}));

    Builder()->addInstruction(cur, ::hilti::instruction::operator_::Begin, val);
    Builder()->addInstruction(end, ::hilti::instruction::operator_::End, val);

    auto loop = mbuilder->pushBuilder("loop");

    Builder()->addInstruction(is_end, ::hilti::instruction::operator_::Equal, cur, end);

    auto blocks = Builder()->addIf(is_end);
    auto done = std::get<0>(blocks);
    auto cont = std::get<1>(blocks);

    mbuilder->popBuilder(loop);

    mbuilder->pushBuilder(cont);

    Builder()->addInstruction(e, ::hilti::instruction::operator_::Deref, cur);

    auto eval = RuntimeHiltiToVal(e, type->YieldType());

    Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                              ::hilti::builder::id::create("LibBro::bro_vector_append"),
                              ::hilti::builder::tuple::create({dst, eval}));

    BroUnref(eval);

    Builder()->addInstruction(cur, ::hilti::instruction::operator_::Incr, cur);

    Builder()->addInstruction(::hilti::instruction::flow::Jump, loop->block());

    mbuilder->popBuilder(cont);

    mbuilder->pushBuilder(done);
    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::AnyConversionFunction(const ::BroType* type)
{
    string idx;
    ;

    if ( type->GetName().size() ) {
        idx = type->GetName();
        auto sm = ::extract_module_name(idx.c_str());
        auto sv = ::extract_var_name(idx.c_str());
        idx = ::util::fmt("%s::%s", sm, sv);
    }

    else {
        ODesc d;
        d.SetShort(1);
        type->Describe(&d);
        idx = d.Description();
    }

    ODesc d;
    d.SetShort(1);
    type->Describe(&d);

    if ( auto to = ModuleBuilder()->lookupNode("any-func-to", idx) )
        return ast::rtti::checkedCast<::hilti::Expression>(to);

    auto fname = ::util::fmt("type_to_any.%s", idx);
    fname = ::util::strreplace(fname, "::", "_");
    fname = ::util::strreplace(fname, " ", "_");
    fname = ::util::strreplace(fname, "\n", "_");

    auto ty_bany =
        ::hilti::builder::reference::type(::hilti::builder::type::byName("LibBro::BroAny"));
    auto ty_bval = ::hilti::builder::type::byName("LibBro::BroVal");

    auto fresult = ::hilti::builder::function::result(ty_bval);
    auto fargs = ::hilti::builder::function::parameter_list();
    fargs = {::hilti::builder::function::parameter("a", ty_bany, true, nullptr)};
    auto func =
        ModuleBuilder()->pushFunction(fname, fresult, fargs, ::hilti::type::function::HILTI_C);

    auto t = Builder()->addTmp("t", HiltiType(type));

    Builder()->addInstruction(t, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::any_to_hilti"),
                              ::hilti::builder::tuple::create({::hilti::builder::id::create("a")}));

    auto v = RuntimeHiltiToVal(t, type);

    Builder()->addInstruction(::hilti::instruction::flow::ReturnResult, v);

    ModuleBuilder()->popFunction();

    auto id = ::hilti::builder::id::create(fname);
    ModuleBuilder()->cacheNode("any-func-to", idx, id);
    return id;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::ToAny(
    std::shared_ptr<::hilti::Expression> val, const ::BroType* type)
{
    auto dst = Builder()->addTmp("dst", ::hilti::builder::reference::type(
                                            ::hilti::builder::type::byName("LibBro::BroAny")));

    ::hilti::builder::type_list targs = {::hilti::builder::caddr::type(),
                                         ::hilti::builder::caddr::type()};
    auto ttype = ::hilti::builder::tuple::type(targs);
    auto tfunc = Builder()->addTmp("tfunc", ttype);
    auto faddr = Builder()->addTmp("faddr", ::hilti::builder::caddr::type());

    auto func = AnyConversionFunction(type);

    Builder()->addInstruction(tfunc, ::hilti::instruction::caddr::Function, func);
    Builder()->addInstruction(faddr, ::hilti::instruction::tuple::Index, tfunc,
                              ::hilti::builder::integer::create(0));

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::any_from_hilti_ref"),
                              ::hilti::builder::tuple::create({val, CreateBroType(type), faddr}));

    return dst;
}


std::shared_ptr<::hilti::Expression> ConversionBuilder::FromAny(
    std::shared_ptr<::hilti::Expression> val, const ::BroType* type)
{
    auto dst = Builder()->addTmp("dst", HiltiType(type));

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::any_to_hilti"),
                              ::hilti::builder::tuple::create({val}));

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::AnyBroType(
    std::shared_ptr<::hilti::Expression> val)
{
    auto dst = Builder()->addTmp("dst", ::hilti::builder::type::byName("LibBro::BroType"));

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::any_bro_type"),
                              ::hilti::builder::tuple::create({val}));

    return dst;
}

shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::BroType* type)
{
    switch ( type->Tag() ) {
    case TYPE_ADDR:
    case TYPE_ANY:
    case TYPE_BOOL:
    case TYPE_COUNT:
    case TYPE_COUNTER:
    case TYPE_DOUBLE:
    case TYPE_FILE:
    case TYPE_INT:
    case TYPE_INTERVAL:
    case TYPE_PATTERN:
    case TYPE_PORT:
    case TYPE_STRING:
    case TYPE_TIME:
    case TYPE_TYPE:
    case TYPE_VOID:
        return CreateBroTypeBase(static_cast<const ::BroType*>(type));

    case TYPE_ENUM:
        return CreateBroType(static_cast<const ::EnumType*>(type));

    case TYPE_LIST:
        return CreateBroType(static_cast<const ::TypeList*>(type));

    case TYPE_OPAQUE:
        return CreateBroType(static_cast<const ::OpaqueType*>(type));

    case TYPE_RECORD:
        return CreateBroType(static_cast<const ::RecordType*>(type));

    case TYPE_SUBNET:
        return CreateBroType(static_cast<const ::SubNetType*>(type));

    case TYPE_TABLE:
        return CreateBroType(static_cast<const ::TableType*>(type));

    case TYPE_VECTOR:
        return CreateBroType(static_cast<const ::VectorType*>(type));

    case TYPE_FUNC:
        return CreateBroType(static_cast<const ::FuncType*>(type));

    default:
        Error(::util::fmt("ConversionBuilder/CBT: unsupported value of type %s",
                          ::type_name(type->Tag())));
    }

    // Cannot be reached.
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroTypeBase(const ::BroType* type)
{
    auto dst = Builder()->addTmp("btype", ::hilti::builder::type::byName("LibBro::BroType"));
    auto tag = ::hilti::builder::integer::create(type->Tag());

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_base_type"),
                              ::hilti::builder::tuple::create({tag}));

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::EnumType* type)
{
    return BuildCreateBroType("enum", type, [&](shared_ptr<::hilti::Expression> dst,
                                                const ::BroType* type) {
        auto name = ::hilti::builder::string::create(type->GetName());
        auto module = ::hilti::builder::string::create(ModuleBuilder()->module()->id()->name());

        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_enum_type_new"),
                                  ::hilti::builder::tuple::create({module, name}));

        for ( auto n : type->AsEnumType()->Names() ) {
            auto name = ::hilti::builder::string::create(n.first);
            auto val = ::hilti::builder::integer::create(n.second);

            Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                      ::hilti::builder::id::create(
                                          "LibBro::bro_enum_type_add_name"),
                                      ::hilti::builder::tuple::create({dst, module, name, val}));
        }
    });
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::TypeList* type)
{
    return BuildCreateBroType("list", type, [&](shared_ptr<::hilti::Expression> dst,
                                                const ::BroType* type) {
        auto ltype = type->AsTypeList();
        auto pure_type =
            ltype->IsPure() ? CreateBroType(ltype->PureType()) : ::hilti::builder::caddr::create();

        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_list_type_new"),
                                  ::hilti::builder::tuple::create({pure_type}));

        auto types = ltype->Types();

        loop_over_list(*types, i)
        {
            auto t = CreateBroType((*types)[i]);
            Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                                      ::hilti::builder::id::create("LibBro::bro_list_type_append"),
                                      ::hilti::builder::tuple::create({dst, t}));
            BroUnref(t);
        }
    });
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::OpaqueType* type)
{
    Error("ConversionBuilder/CBT: no support yet for converting OpaqueType");
    return nullptr;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::RecordType* type)
{
    return BuildCreateBroType("record", type, [&](shared_ptr<::hilti::Expression> dst,
                                                  const ::BroType* type) {
        auto rtype = type->AsRecordType();
        auto dtype = ::hilti::builder::type::byName("LibBro::BroTypeDecl");
        auto ltype = ::hilti::builder::list::type(dtype);
        auto rltype = ::hilti::builder::reference::type(ltype);

        auto fields = Builder()->addTmp("fields", rltype);
        auto field = Builder()->addTmp("field", dtype);

        Builder()->addInstruction(fields, ::hilti::instruction::list::New,
                                  ::hilti::builder::type::create(ltype));

        for ( int i = 0; i < rtype->NumFields(); i++ ) {
            auto fname = ::hilti::builder::string::create(rtype->FieldName(i));
            auto ftype = CreateBroType(rtype->FieldType(i));

            Builder()->addInstruction(field, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create(
                                          "LibBro::bro_record_typedecl_new"),
                                      ::hilti::builder::tuple::create({fname, ftype}));

            Builder()->addInstruction(::hilti::instruction::list::PushBack, fields, field);
        }

        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_record_type_new"),
                                  ::hilti::builder::tuple::create({fields}));

    });
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::SubNetType* type)
{
    auto dst = Builder()->addTmp("btype", ::hilti::builder::type::byName("LibBro::BroType"));

    Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                              ::hilti::builder::id::create("LibBro::bro_subnet_type_new"),
                              ::hilti::builder::tuple::create({}));

    return dst;
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::TableType* type)
{
    if ( type->IsSet() ) {
        return BuildCreateBroType("set", type, [&](shared_ptr<::hilti::Expression> dst,
                                                   const ::BroType* type) {
            auto ttype = type->AsTableType();

            auto k = CreateBroType(ttype->Indices());
            auto null = ::hilti::builder::caddr::create();

            Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create("LibBro::bro_table_type_new"),
                                      ::hilti::builder::tuple::create({k, null}));
        });
    }

    else {
        return BuildCreateBroType("table", type, [&](shared_ptr<::hilti::Expression> dst,
                                                     const ::BroType* type) {
            auto ttype = type->AsTableType();

            auto k = CreateBroType(ttype->Indices());
            auto v = CreateBroType(ttype->YieldType());

            Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                      ::hilti::builder::id::create("LibBro::bro_table_type_new"),
                                      ::hilti::builder::tuple::create({k, v}));
        });
    }
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::VectorType* type)
{
    return BuildCreateBroType("vector", type, [&](shared_ptr<::hilti::Expression> dst,
                                                  const ::BroType* type) {
        auto ytype = CreateBroType(type->AsVectorType()->YieldType());

        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_vector_type_new"),
                                  ::hilti::builder::tuple::create({ytype}));
    });
}

std::shared_ptr<::hilti::Expression> ConversionBuilder::CreateBroType(const ::FuncType* type)
{
    return BuildCreateBroType("function", type, [&](shared_ptr<::hilti::Expression> dst,
                                                    const ::BroType* type) {
        auto ftype = type->AsFuncType();
        auto args = CreateBroType(ftype->Args());
        auto yield = ftype->YieldType() ? CreateBroType(ftype->YieldType()) :
                                          ::hilti::builder::caddr::create();
        auto flavor = ::hilti::builder::integer::create(ftype->Flavor());

        Builder()->addInstruction(dst, ::hilti::instruction::flow::CallResult,
                                  ::hilti::builder::id::create("LibBro::bro_function_type_new"),
                                  ::hilti::builder::tuple::create({args, yield, flavor}));
    });
}

void ConversionBuilder::BroRef(std::shared_ptr<::hilti::Expression> val)
{
    ::hilti::builder::tuple::element_list args = {val};

    Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                              ::hilti::builder::id::create("LibBro::object_ref"),
                              ::hilti::builder::tuple::create(args));
}

void ConversionBuilder::BroUnref(std::shared_ptr<::hilti::Expression> val)
{
    ::hilti::builder::tuple::element_list args = {val};

    Builder()->addInstruction(::hilti::instruction::flow::CallVoid,
                              ::hilti::builder::id::create("LibBro::object_unref"),
                              ::hilti::builder::tuple::create(args));
}
