///
/// \type Tuple
///
/// The *tuple* data type represents ordered tuples of other values, which
/// can be of mixed type. Tuple are however statically types and therefore
/// need the individual types to be specified as type parameters, e.g.,
/// ``tuple<int32,string,bool>`` to represent a 3-tuple of ``int32``,
/// ``string``, and ``bool``. Tuple values are enclosed in parentheses with
/// the individual components separated by commas, e.g., ``(42, "foo",
/// True)``. If not explictly initialized, tuples are set to their
/// components' default values initially.


iBegin(tuple::Equal, "equal")
    iTarget(optype::boolean);
    iOp1(optype::tuple, true);
    iOp2(optype::tuple, true);

    iValidate
    {
        equalTypes(op1, op2);
        // TODO: Check that we can compare the tuple element recursively.
    }

    iDoc(R"(
        Returns True if the tuple in *op1* equals the tuple in *op2*.
    )");
iEnd

iBegin(tuple::Index, "tuple.index")
    iTarget(optype::any);
    iOp1(optype::tuple, true);
    iOp2(optype::integer, true);

    iValidate
    {
        auto ty_target = target->type();
        auto ttype = ast::rtti::tryCast<type::Tuple>(op1->type());

        isConstant(op2);

        auto c = ast::rtti::tryCast<expression::Constant>(op2)->constant();
        auto idx = ast::rtti::tryCast<constant::Integer>(c)->value();

        if ( idx < 0 || (size_t)idx >= ttype->typeList().size() ) {
            error(op2, "tuple index out of range");
            return;
        }

        int i = 0;
        for ( auto t : ttype->typeList() ) {
            if ( i++ == idx )
                canCoerceTo(t, target);
        }
    }

    iDoc(R"(
       Returns the tuple's value with index *op2*. The index is zero-based.
       *op2* must be an integer *constant*.
    )");
iEnd

iBegin(tuple::Length, "tuple.length")
    iTarget(optype::int64);
    iOp1(optype::tuple, true);

    iValidate
    {
    }

    iDoc(R"(
       Returns the number of elements that the tuple *op1* has.
    )");
iEnd
