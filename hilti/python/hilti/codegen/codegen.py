#! /usr/bin/env
#
# LLVM code geneator.
import sys

import llvm
import llvm.core 

global_id = id

from hilti.core import *
from hilti import instructions

# Class which just absorbs any method calls.
class _DummyBuilder(object):
    def __getattribute__(*args):
        class dummy:
            pass
        return lambda *args: dummy()
    
### Codegen visitor.

class CodeGen(visitor.Visitor):
    def __init__(self):
        super(CodeGen, self).__init__()
        self.reset()
        
    def reset(self):
        """Resets internal state. After resetting, the object can be used to
        turn another |ast| into LLVM code.
        """
        # Dummy class to create a sub-namespace.
        class _llvm_data:
            pass

        # These state variables are cleared here. Read access must only be via
        # methods of this class. However, some of them are *set* directly by the
        # module/function/block visitors, which avoids adding a ton of setter
        # methods just for these few cases. 
        
        self._module = None        # Current module.
        self._function = None      # Current function.
        self._block = None         # Current block.
        self._func_counter = 0     # Counter to generate unique function names.
        self._label_counter = 0    # Counter to generate unique labels.
        self._const_counter = 0    # Counter to generate unique constant names.
        
        self._llvm = _llvm_data()
        self._llvm.module = None   # Current LLVM module.
        self._llvm.functions = {}  # All LLVM functions created so far, indexed by their name.
        self._llvm.frameptr = None # Frame pointer for current LLVM function.
        self._llvm.builders = []   # Stack of llvm.core.Builders

        # Cache some LLVM types.
         
            # The type we use a generic pointer ("void *").
        self._llvm.type_generic_pointer = llvm.core.Type.pointer(llvm.core.Type.int(8)) 

            # A continuation struct.
        self._llvm.type_continuation = llvm.core.Type.struct([
        	self._llvm.type_generic_pointer, # Successor function
            self._llvm.type_generic_pointer  # Frame pointer
            ])

            # Field names with types for the basic frame.
        self._bf_fields = [
        	("cont_normal", self._llvm.type_continuation), 
        	("cont_except", self._llvm.type_continuation),    
		    ("exception", self._llvm.type_generic_pointer),
            ]

            # The basic frame struct. 
        self._llvm.type_basic_frame = llvm.core.Type.struct([ t for (n, t) in self._bf_fields])

    def generateLLVM(self, ast, verify=True):
        """See ~~generateLLVM."""
        self.reset()
        self.visit(ast)
        return self.llvmModule(verify)
            
    def pushBuilder(self, llvm_block):
        """Pushes a LLVM builder on the builder stack. The method creates a
        new ``llvm.core.Builder`` with the given block (which will often be
        just empty at this point), and pushes it on the stack. All ~~Codegen
        methods creating LLVM instructions use the most recently pushed
        builder.
        
        llvm_blockbuilder: llvm.core.Block - The block to create the builder
        with.
        """
        builder = llvm.core.Builder.new(llvm_block) if llvm_block else _DummyBuilder()
        self._llvm.builders += [builder]
        
    def popBuilder(self):
        """Removes the top-most LLVM builder from the builder stack."""
        self._llvm.builders = self._llvm.builders[0:-1]
    
    def builder(self):
        """Returns the top-most LLVM builder from the builder stack. Methods
        creating LLVM instructions should usually use this builder.
        
        Returns: llvm.core.Builder: The most recently pushed builder.
        """
        return self._llvm.builders[-1]

    def currentModule(self):
        """Returns the current module. 
        
        Returns: ~~Module - The current module.
        """
        return self._module
    
    def currentFunction(self):
        """Returns the current function. 
        
        Returns: ~~Function - The current function.
        """
        return self._function
    
    def currentBlock(self):
        """Returns the current block. 
        
        Returns: ~~Block - The current function.
        """
        return self._block
    
    # Looks up the function belonging to the given name. Returns None if not found. 
    def lookupFunction(self, name):
        """Looks up the function with the given name in the current module.
        This is a short-cut for ``currentModule()->LookupIDVal()`` which in
        addition checks (via asserts) that the returns object is indeed a
        function.
        
        Returns: ~~Function - The function with the given name, or None if
        there's no such identifier in the current module's scope.
        """
        func = self._module.lookupIDVal(name)
        assert (not func) or (isinstance(func.type(), type.Function))
        return func
    
    def nameFunction(self, func):
        """Returns the internal LLVM name for the function. The method
        processes the function's name according to HILTI's name mangling
        scheme.
        
        Returns: string - The mangled name, to be used for the corresponding 
        LLVM function.
        """
        name = func.name().replace("::", "_")
        
        if func.callingConvention() == function.CallingConvention.C:
            # Don't mess with the names of C functions.
            return name
        
        if func.callingConvention() == function.CallingConvention.HILTI:
            return "hlt_%s" % name
        
        # Cannot be reached
        assert False
            

    def nameFunctionForBlock(self, block):
    	"""Returns the internal LLVM name for the block's function. The
        compiler turns all blocks into functions, and this method returns the
        name which the LLVM function for the given block should have.
        
        Returns: string - The name of the block's function.
        """
        
        function = block.function()
        funcname = self.nameFunction(function)
        name = block.name()
        
        first_block = function.blocks()[0].name()

        if name == first_block:
            return funcname
        
        if name.startswith("__"):
            name = name[2:]
    
        return "__%s_%s" % (funcname, name)

    def nameNewLabel(self, postfix=None):
        """Returns a unique label for LLVM blocks. The label is guaranteed to
        be unique within the :meth:`currentFunction``.
        
        postfix: string - If given, the postfix is appended to the generated label.
        
        Returns: string - The unique label.
        """ 
        self._label_counter += 1
        
        if postfix:
            return "l%d-%s" % (self._label_counter, postfix)
        else:
            return "l%d" % self._label_counter
        
    def nameNewConstant(self, postfix=None):
        """Returns a unique name for global constant. The name is guaranteed
        to be unique within the :meth:`currentModule``.
        
        postfix: string - If given, the postfix is appended to the generated name.
        
        Returns: string - The unique name.
        """ 
        self._const_counter += 1
        
        if postfix:
            return "c%d-%s" % (self._const_counter, postfix)
        else:
            return "c%d" % self._const_counter
    
    def nameNewFunction(self, prefix):
        """Returns a unique name for LLVM functions. The name is guaranteed to
        unique within the :meth:`currentModuleFunction``.
        
        prefix: string - If given, the prefix is prepended before the generated name.
        
        Returns: string - The unique name.
        """ 
        self._func_counter += 1
        
        if prefix:
            return "__%s_%s_f%s" % (self._module.name(), prefix, self._func_counter)
        else:
            return "__%s_f%s" % (self._module.name(), self._func_counter)
    
    def llvmNewBlock(self, postfix):
        """Creates a new LLVM block. The block gets a label that is guaranteed
        to be unique within the :meth:`currentFunction``, and it is added to
        the function's implementation.
        
        postfix: string - If given, the postfix is appended to the block's
        label.
        
        Returns: llvm.core.Block - The new block.
        """         
        return self._llvm.func.append_basic_block(self.nameNewLabel(postfix))

    def nameFunctionFrame(self, function):
        """Returns the LLVM name for the struct representing the function's
        frame.
        
        Returns: string - The name of the struct.
        """
        return "__frame_%s" % self.nameFunction(function)
    
    def llvmModule(self, verify=True):
        """Returns the compiled LLVM module. Can only be called once code
        generation has already finished.
        
        *verify*: bool - If true, the correctness of the generated LLVM module
        will be verified via LLVM's internal validator.
        
        Returns: tuple (bool, llvm.core.Module) - If the bool is True, code
        generation (and if *verify* is True, also verification) was
        successful. If so, the second element of the tuple is the resulting
        LLVM module.
        """
        if verify:
            try:
                self._llvm.module.verify()
            except llvm.LLVMException, e:
                util.error("LLVM error: %s" % e, fatal=False)
                return (False, None)
            
        return (True, self._llvm.module)

    def llvmCurrentModule(self):
        """Returns the current LLVM module.
        
        Returns: llvm.core.Module - The LLVM module we're currently building.
        """
        return self._llvm.module
    
    def llvmCurrentFramePtr(self):
        """Returns the frame pointer for the current LLVM function. The frame
        pointer is the base pointer for all accesses to local variables,
        including function arguments). The execution model always passes the
        frame pointer into a function as its first parameter.
        
        Returns: llvm.core.Type.struct - The current frame pointer.
        """
        return self._llvm.frameptr
    
    # Return the LLVM constant representing the given integer.
    def llvmConstInt(self, n, width = 32):
        """Creates an LLVM integer constant.
        
        n: integer - The value of the constant.
        width: interger - The bit-width of the constant.
        
        Returns: llvm.core.Constant.int - The constant.
        """
        return llvm.core.Constant.int(llvm.core.Type.int(width), n)

    # Returns the LLVM constant representing an unset exception.
    def llvmConstNoException(self):
        """Returns the LLVM constant representing an unset exception. This
        constant is written into the basic-frame's exeception field to
        indicate that no exeception has been raised.
        
        Returns: llvm.core.Constant - The constant matching the
        basic-frame's exception field.
        """
        return llvm.core.Constant.null(self.llvmTypeGenericPointer())

    # Returns the type used to represent pointer of different type. 
    # (i.e., "void *")
    def llvmTypeGenericPointer(self):
        """Returns the LLVM type that we use for representing a generic
        pointer. (\"``void *``\").
        
        Returns: llvm.core.Type.pointer - The pointer type.
        """
        return self._llvm.type_generic_pointer

    # Returns a type representing a pointer to the given function. 
    def llvmTypeFunctionPtr(self, function):
        """Returns the LLVM type for representing a pointer to the function.
        The type is built according to the function's signature and the
        internal LLVM execution model.
        
        Returns: llvm.core.Type.pointer - The function pointer type.
        """
        voidt = llvm.core.Type.void()
        ft = llvm.core.Type.function(voidt, [llvm.core.Type.pointer(self.llvmTypeFunctionPtrFrame(function))])
        return llvm.core.Type.pointer(llvm.core.Type.pointer(ft))

    def llvmTypeBasicFunctionPtr(self, args=[]):
        """Returns the LLVM type for representing a pointer to an LLVM
        function taking a basic-frame as its argument. Additional additional
        can be optionally given as well. The function type will have ``void``
        as it's return type.
        
        args: list of ~~Type - Additional arguments for the function, which
        will be converted into the corresponding LLVM types.
        
        Returns: llvm.core.Type.pointer - The function pointer type. 
        """
        voidt = llvm.core.Type.void()
        args = [self.llvmTypeConvert(t) for t in args]
        bf = [llvm.core.Type.pointer(self.llvmTypeBasicFrame())]
        ft = llvm.core.Type.function(voidt, bf + args)
        return llvm.core.Type.pointer(llvm.core.Type.pointer(ft))
    
    def llvmTypeContinuation(self):
        """Returns the LLVM type used for representing continuations.
        
        Returns: llvm.core.Type.struct - The type for a continuation.
        """
        return self._llvm.type_continuation
    
    def llvmTypeBasicFrame(self):
        """Returns the LLVM type used for representing a basic-frame.
        
        Returns: llvm.core.Type.struct - The type for a basic-frame.
        """
        return self._llvm.type_basic_frame

    def llvmTypeFunctionFrame(self, function):
        """Returns the LLVM type used for representing the function's frame.
        
        Returns: llvm.core.Type.struct - The type for the function's frame.
        """
        
        try:
            # Is it cached?
            if function._llvm_frame != None:
                return function._llvm_frame
        except AttributeError:
            pass
        
        locals = [i for i in function.IDs() if isinstance(i.type(), type.StorageType)]
        fields = self._bf_fields + [(i.name(), self.llvmTypeConvert(i.type())) for i in locals]

        # Build map of indices and store with function.
        function._frame_index = {}
        for i in range(0, len(fields)):
            function._frame_index[fields[i][0]] = i

        frame = [f[1] for f in fields]
        function._llvm_frame = llvm.core.Type.struct(frame) # Cache it.
        
        return function._llvm_frame

    # Helpers to get a field in a basic frame struct.
    def _llvmAddrInBasicFrame(self, frame, index1, index2, cast_to, tag):
        zero = self.llvmConstInt(0)
        index1 = self.llvmConstInt(index1)
        
        if index2 >= 0:
            index2 = self.llvmConstInt(index2)
            index = [zero, index1, index2]
        else:
            index = [zero, index1]
        
        if not cast_to:
            addr = self.builder().gep(frame, index, tag)
            return addr
        else:
            addr = self.builder().gep(frame, index, "tmp" + tag)
            return self.builder().bitcast(addr, cast_to, tag)
    
    # Returns the address of the default continuation's successor field in the given frame.
    def llvmAddrDefContSuccessor(self, frameptr, cast_to = None):
        """Returns the address of the default continuation's successor field
        inside a function's frame. The method uses the current :meth:`builder`
        to calculate the address and returns an LLVM value object that can be
        directly used with subsequent LLVM load or store instructions.
        
        frameptr: llvm.core.* - The address of the function's frame. This
        must be an LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Examples include corresponding ``llvm.core.Argument`` and
        ``llvm.core.Instruction`` objects. 
        
        cast_to: llvm.core.Type.pointer - If given, the address will be
        bit-casted to the given pointer type.
        
        Returns: llvm.core.Value - An LLVM value with the address. If
        *cast_to* was not given, its type will be a pointer to a
        :meth:`llvmTypeGenericPointer`. 
        """
        return self._llvmAddrInBasicFrame(frameptr, 0, 0, cast_to, "cont_succ")
    
    # Returns the address of the default continuation's frame field in the given frame.
    def llvmAddrDefContFrame(self, frameptr, cast_to = None):
        """Returns the address of the default continuation's frame field
        inside a function's frame. The method uses the current :meth:`builder`
        to calculate the address and returns an LLVM value object that can be
        directly used with subsequent LLVM load or store instructions.
        
        frameptr: llvm.core.* - The address of the function's frame. This
        must be an LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Examples include corresponding ``llvm.core.Argument`` and
        ``llvm.core.Instruction`` objects. 
        
        cast_to: llvm.core.Type.pointer - If given, the address will be
        bit-casted to the given pointer type.
        
        Returns: llvm.core.Value - An LLVM value with the address. If
        *cast_to* was not given, its type will be a pointer to a
        :meth:`llvmTypeGenericPointer`. 
        """
        return self._llvmAddrInBasicFrame(frameptr, 0, 1, cast_to, "cont_frame")

    # Returns the address of the exception continuation's successor field in the given frame.
    def llvmAddrExceptContSuccessor(self, frameptr, cast_to = None):
        """Returns the address of the exceptional continuation's successor
        field inside a function's frame. The method uses the current
        :meth:`builder` to calculate the address and returns an LLVM value
        object that can be directly used with subsequent LLVM load or store
        instructions.
        
        frameptr: llvm.core.* - The address of the function's frame. This
        must be an LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Examples include corresponding ``llvm.core.Argument`` and
        ``llvm.core.Instruction`` objects. 
        
        cast_to: llvm.core.Type.pointer - If given, the address will be
        bit-casted to the given pointer type.
        
        Returns: llvm.core.Value - An LLVM value with the address. If
        *cast_to* was not given, its type will be a pointer to a
        :meth:`llvmTypeGenericPointer`. 
        """
        return self._llvmAddrInBasicFrame(frameptr, 1, 0, cast_to, "excep_succ")
    
    # Returns the address of the exception continuation's frame field in the given frame.
    def llvmAddrExceptContFrame(self, frameptr, cast_to = None):
        """Returns the address of the exceptional continuation's frame field
        inside a function's frame. The method uses the current :meth:`builder`
        to calculate the address and returns an LLVM value object that can be
        directly used with subsequent LLVM load or store instructions.
        
        frameptr: llvm.core.* - The address of the function's frame. This must be an
        LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Examples include corresponding ``llvm.core.Argument`` and
        ``llvm.core.Instruction`` objects. 
        
        cast_to: llvm.core.Type.pointer - If given, the address will be
        bit-casted to the given pointer type.
        
        Returns: llvm.core.Value - An LLVM value with the address. If
        *cast_to* was not given, its type will be a pointer to a
        :meth:`llvmTypeGenericPointer`. 
        """
        return self._llvmAddrInBasicFrame(frameptr, 1, 1, cast_to, "excep_frame")

    # Returns the address of the current exception field in the given frame.
    def llvmAddrException(self, frameptr):
        """Returns the address of the exception field inside a function's
        frame. The method uses the current :meth:`builder` to calculate the
        address and returns an LLVM value object that can be directly used
        with subsequent LLVM load or store instructions.
        
        frameptr: llvm.core.* - The address of the function's frame. This
        must be an LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Examples include corresponding ``llvm.core.Argument`` and
        ``llvm.core.Instruction`` objects. 
        
        cast_to: llvm.core.Type.pointer - If given, the address will be
        bit-casted to the given pointer type.
        
        Returns: llvm.core.Value - An LLVM value with the address. If
        *cast_to* was not given, its type will be a pointer to a
        :meth:`llvmTypeGenericPointer`. 
        """
        return self._llvmAddrInBasicFrame(frameptr, 2, -1, None, "excep_frame")
    
    def llvmAddrLocalVar(self, function, frameptr, name):
        """Returns the address of a local variable inside a function's frame.
        The method uses the current :meth:`builder` to calculate the address
        and returns an LLVM value object that can be directly used with
        subsequent LLVM load or store instructions.
        
        function: ~~Function - The function whose local variable should be
        accessed.
        
        frameptr: llvm.core.* - The address of the function's frame. This must
        be an LLVM object that can act as the frame's address in the LLVM
        instructions that will be built to calculate the field's address.
        Often, it will be the result of :meth:`llvmCurrentFramePtr`.
        
        name: string - The name of the local variable. 

        Returns: llvm.core.Value - An LLVM value with the address. 
        """
        try:
            # See whether we already calculated the field index dict. 
            frame_idx = function._frame_index
        except AttributeError:
            # Not yet calculated.
            self.llvmTypeFunctionFrame(function)
            frame_idx = function._frame_index
            
        # The Python interface (as well as LLVM's C interface, which is used
        # by the Python interface) does not have builder.extract_value() method,
        # so we simulate it with a gep/load combination.
        zero = self.llvmConstInt(0)
        index = self.llvmConstInt(frame_idx[name])
        return self.builder().gep(frameptr, [zero, index], name)

    def llvmGetGlobalVar(self, name, type):
        """Generates an LLVM load instruction for reading a global variable.
        The global can be either defined in the current module, or externally
        (the latter is assumed if we don't fine the variable locally). The
        load is built via the current :meth:`builder`.
        
        name: string - The internal LLVM name of the global. 
        
        type: llvm.core.Type - The LLVM type of the global.
        
        Returns: llvm.core.Value - The result of the LLVM load instruction. 
        """
        try:
            glob = self._llvm.module.get_global_variable_named(name)
            # FIXME: Check that the type is the same.
            return self.builder().load(glob, name)
        except llvm.LLVMException:
            pass
        
        glob = self._llvm.module.add_global_variable(type, name)
        glob.linkage = llvm.core.LINKAGE_EXTERNAL 
        return self.builder().load(glob, name)

    # Creates a new function matching the block's frame/result. Additional
    # arguments to the function can be specified by passing (name, type)
    # tuples via addl_args.
    def llvmCreateFunction(self, hilti_func, name = None, addl_args = []):
        """Creates a new LLVM function. The new function's signatures is built
        from the given HILTI function, according to the HILTI calling
        conventions. The function is added to :meth:`llvmCurrentModule`.
        
        hilti_func: ~~Function - The HILTI function determining the signature. 
        
        name: string - If given, the name of the new function. If not given,
        the result of :meth:`nameFunction` is used. 
        
        addl_args: list of (name, type) - Additional arguments to add to new
        the function's argument list. *name* is a ``string`` with the argument's
        name, and *type* is a ``llvm.core.Type`` with the argument's type.
        
        Returns: llvm.core.Function - The new LLVM function. 
        """

        if not name:
            name = self.nameFunction(hilti_func)
        
        # Build function type.
        args = [llvm.core.Type.pointer(self.llvmTypeFunctionFrame(hilti_func))]
        ft = llvm.core.Type.function(llvm.core.Type.void(), args + [t for (n, t) in addl_args]) 
        
        # Create function.
        func = self._llvm.module.add_function(ft, name)
        func.calling_convention = llvm.core.CC_FASTCALL
        
        if hilti_func.linkage() != function.Linkage.EXPORTED:
            func.linkage = llvm.core.LINKAGE_INTERNAL
        
        # Set parameter names. 
        func.args[0].name = "__frame"
        for i in range(len(addl_args)):
            func.args[i+1].name = addl_args[i][0]
        
        self._llvm.functions[name] = func

        return func

    def llvmGetFunction(self, func):
        """Returns the LLVM function for the given function. If the LLVM
        function doesn't exist yet, it will be created. This method can only be
        used for functions with ~~Linkage ~~HILTI.
        
        func: ~~Function - The function. 
        
        Returns: llvm.core.Function - The LLVM function.
        """
        assert func.callingConvention() == function.CallingConvention.HILTI
        
        try:
            name = self.nameFunction(func)
            return self._llvm.functions[name]
        except KeyError:        
            return self.llvmCreateFunction(func)
    
    def llvmGetFunctionForBlock(self, block):
        """Returns the LLVM function for a block. If the LLVM function doesn't
        exist yet, it will be created.
        
        block: ~~Block - The block for which the LLVM function should be
        returned.
        
        Returns: llvm.core.Function - The LLVM function for the block.
        """
        name = self.nameFunctionForBlock(block) 
        try:
            return self._llvm.functions[name]
        except KeyError:
            return self.llvmCreateFunction(block.function(), name)

    def llvmGetCFunction(self, func):
        """Returns the LLVM function for an external C function. If the LLVM
        function doesn't exist yet, it will be created.
        
        func: ~~Function - The function which must be of ~~CallingConvention ~~C.
        
        Returns: llvm.core.Function - The LLVM function for the external C
        function.
        """
                
        assert func.callingConvention() == function.CallingConvention.C
        
        name = func.name().replace("::", "_")
        
        try:
            func = self._llvm.functions[name]
            # Make sure it's a C function.
            assert func.calling_convention == llvm.core.CC_C
            return func
        
        except KeyError:
            rt = self.llvmTypeConvert(func.type().resultType())
            args = [self.llvmTypeConvertToC(id.type()) for id in func.type().Args()]
            ft = llvm.core.Type.function(rt, args)
            llvmfunc = self._llvm.module.add_function(ft, name)
            llvm.calling_convention = llvm.core.CC_C
            self._llvm.functions[name] = llvmfunc
            return llvmfunc

    def llvmGenerateCCall(self, function, args):
        """Generates a call to a C function. The method uses the current
        :meth:`builder`.
        
        function: ~~Function - The function, which must have ~~C ~~Linkage.
        args: list of llvm.core.Value - The arguments to pass to the function.
        
        Todo: If the LLVM type differs from the one supposed to be used for
        the C call, we don't convert yet and thus will likely get a ``bad
        signature`` assertion. See ~~llvmOpToC.
        """
        
        llvm_func = self.llvmGetCFunction(function)
        
        tag = None
        if function.type().resultType() == type.Void:
            call = self.builder().call(llvm_func, args)
        else:
            call = self.builder().call(llvm_func, args, "result")
        
        call.calling_convention = llvm.core.CC_C

    def llvmRaiseException(self, exception):
        """Generates the raising of an exception. The method uses the current
        :meth:`builder`.
        
	    exception: string - The name of the internal global variable
        representing the exception to raise. These names are defined in 
        :path:`libhilti/exceptions.cc`.
        
        Note: Exception support is preliminary and will very likely change at
        some point in its specifics.
        """
        
        # ptr = *__frame.bf.cont_exception.succesor
        fpt = self.llvmTypeBasicFunctionPtr()
        addr = self.llvmAddrExceptContSuccessor(self._llvm.frameptr, fpt)
        ptr = self.builder().load(addr, "succ_ptr")
    
        # frame = *__frame.bf.cont_exception
        bfptr = llvm.core.Type.pointer(self.llvmTypeBasicFrame())
        addr = self.llvmAddrExceptContFrame(self._llvm.frameptr, llvm.core.Type.pointer(bfptr))
        frame = self.builder().load(addr, "frame_ptr")
    
        # frame.exception = <exception>
        addr = self.llvmAddrException(frame)
        exc = self.llvmGetGlobalVar(exception, self.llvmTypeGenericPointer())
        self.llvmInit(exc, addr)
        
        self.llvmGenerateTailCallToFunctionPtr(ptr, [frame])
        
    # Generates LLVM tail call code. From the LLVM 1.5 release notes:
    #
    #    To ensure a call is interpreted as a tail call, a front-end must
    #    mark functions as "fastcc", mark calls with the 'tail' marker,
    #    and follow the call with a return of the called value (or void).
    #    The optimizer and code generator attempt to handle more general
    #    cases, but the simple case will always work if the code
    #    generator supports tail calls. Here is an example:
    #
    #    fastcc int %bar(int %X, int(double, int)* %FP) {       ; fastcc
    #        %Y = tail call fastcc int %FP(double 0.0, int %X)  ; tail, fastcc
    #        ret int %Y
    #    }
    #
    # FIXME: How do we get the "tail" modifier in there?

    def _llvmGenerateTailCall(self, llvm_func, args):
        result = self.builder().call(llvm_func, args)
        result.calling_convention = llvm.core.CC_FASTCALL
        self.builder().ret_void()

        # Any subsequent code generated inside the same block will
        # be  unreachable. We create a dummy builder to absorb it; the code will
        # not appear anywhere in the output.
        self.popBuilder();
        self.pushBuilder(None)
        
    def llvmGenerateTailCallToBlock(self, block, args):
        """Generates a tail call to a block's function. We generate a separate
        LLVM function for each ~~Block, which can then be called with via this
        method. The method uses the current :meth:`builder`.
        
        block: ~~Block - The block to call the function for. 
        args: list of llvm.core.Value - The arguments to pass to the function.
        """
        llvm_func = self.llvmGetFunctionForBlock(block)
        self._llvmGenerateTailCall(llvm_func, args)
            
    def llvmGenerateTailCallToFunction(self, func, args):
        """Generates a tail call to a function. The function must have
        ~~Linkage ~HILTI. The method uses the current :meth:`builder`. 
        
        function: ~~Function - The function, which must have ~~HILTI ~~Linkage.
        args: list of llvm.core.Value - The arguments to pass to the function.
        """
        assert func.callingConvention() == function.CallingConvention.HILTI
        llvm_func = self.llvmGetFunction(func)
        assert llvm_func
        self._llvmGenerateTailCall(llvm_func, args)

    def llvmGenerateTailCallToFunctionPtr(self, ptr, args):
        """Generates a tail call to a function pointer. The function being
        pointed to must have ~~Linkage ~HILTI. The method uses the current
        :meth:`builder`. 
        
        function: ~~llvm.core.Pointer - The pointer to a function. 
        args: list of llvm.core.Value - The arguments to pass to the function.
        """
        self._llvmGenerateTailCall(ptr, args)
        
    def llvmInit(self, value, addr):
        """Initializes a formerly unused address with a value. This
        essentially acts like a ``builder().store(value, addr)``. However, it
        must be used instead of that to indicate that a formerly
        non-initialized variable is being written to the first time. 
        
        Note: Currently, this is in fact just a ``store()`` operation. In the
        future however we might add more logic here, e.g., if using reference
        counting.
        
        Todo: This is not called for function parameters/returns yet.
        """
        self.builder().store(value, addr)
        
    def llvmAssign(self, value, addr):
        """Assignes a value to an address that has already been initialized
        earlier. This essentially acts like a ``builder().store(value,
        addr)``. However, it must be used instead of that to indicate that a
        formerly initialized variable is being changed. 
        
        Note: Currently, this is in fact just a ``store()`` operation. In the
        future however we might add more logic here, e.g., if using reference
        counting.
        
        Todo: This is not called for function parameters/returns yet.
        """
        self.builder().store(value, addr)
            
    def llvmMalloc(self, type):
        """Allocates memory for the given type. This essentially acts like a
        ``builder().malloc(type)``. However, it must be used instead of
        that in case further tasks are associated with such a malloc.

        Note: Currently, this is in fact just a ``malloc()`` operation. In the
        future however we might add more logic here, e.g., if using garbage
        collection.
        """
        return self.builder().malloc(type)
        
    def llvmStoreInTarget(self, target, val):
        """Stores a value in a target operand. The method uses the current
        :meth:`builder`. 
        
        target: ~~IDOperand - The target operand. 
        val: llvm.core.Value - The value to store.
        
        Todo: Currently, stores are only supported for local variables, not
        globals.
        """
        if isinstance(target, instruction.IDOperand):
            i = target.id()
            if i.role() == id.Role.LOCAL or i.role() == id.Role.PARAM:
                # A local variable.
                addr = self.llvmAddrLocalVar(self._function, self._llvm.frameptr, i.name())
                self.llvmAssign(val, addr)
                return 
                
        util.internal_error("targetToLLVM: unknown target class: %s" % target)

    # Table of callbacks for conversions. 
    _CONV_CONST_TO_LLVM = 1
    _CONV_TYPE_TO_LLVM = 2
    _CONV_TYPE_TO_LLVM_C = 3
    
    _Conversions = { _CONV_CONST_TO_LLVM: [], _CONV_TYPE_TO_LLVM: [], _CONV_TYPE_TO_LLVM_C: [] }

    def _callConvCallback(self, kind, type, args, must_find=True):
        for (t, func) in CodeGen._Conversions[kind]:
            if isinstance(type, t):
                return func(*args)
            
        if must_find:
            util.internal_error("_callConvCallback: unsupported object %s for conversion type %d" % (repr(obj), kind))
            
        return None
    
    def llvmOp(self, op, cast_to=None):
        """Converts an instruction operand into an LLVM value. The method
        might use the current :meth:`builder`. 
        
        op: ~~Operand - The operand to be converted.
        
        cast_to: ~~Type - A type to which the operand's value should be
        converted to first. Note that supported casting depends on the type,
        and is generally extremely limited and will fail except for a few very
        specific cases.
        
        Returns: llvm.core.Value - The LLVM value of the operand that can then
        be used, e.g., in subsequent LLVM load and store instructions.
        
        Todo: We currently support only operands type ~~IDOperand and
        ~~ConstOperand, and for ~~IDOperand only those referencing local
        variables.
        """

        type = op.type()
        
        if isinstance(op, instruction.ConstOperand):
            return self._callConvCallback(CodeGen._CONV_CONST_TO_LLVM, type, [op, cast_to])

        assert (not cast_to or cast_to == type)
        
        if isinstance(op, instruction.IDOperand):
            i = op.id()
            if i.role() == id.Role.LOCAL or i.role() == id.Role.PARAM:
                # A local variable.
                addr = self.llvmAddrLocalVar(self._function, self._llvm.frameptr, i.name())
                return self.builder().load(addr, "op")

            util.internal_error("llvmOp: unsupported ID operand type %s" % type)
            
        util.internal_error("llvmOp: unsupported operand type %s" % type)

    def llvmOpToC(self, op, cast_to=None):
        """Converts an instruction operand into an LLVM value suitable to pass
        to a C function. In general, a C function may expected a different
        type for a parameter than a HILTI function would for the same
        argument. 
        
        op: ~~Operand - The operand to be converted.
        
        cast_to: ~~Type - A type to which the operand's value should be
        converted first. Note that casting is extremely limited: the only
        supported case is casting integer constants that don't have a width
        specified to an integer that has. 
        
        Returns: llvm.core.Value - The LLVM value of the operand that can then
        be used, e.g., in subsequent LLVM load and store instructions.
        
        Todo: At the moment this is in fact just an alias for ~~llvmOp; for
        our current types that's sufficient. However, we will likely need to
        implement a separate type conversion at some point. 
        """
        return self.llvmOp(op, cast_to)

    def llvmTypeConvert(self, type):
        """Converts a StorageType into the LLVM type used for corresponding
        variable declarations.
        
        type: ~~StorageType - The type to converts.
        
        Returns: llvm.core.Type - The corresponding LLVM type for variable declarations.
        """ 
        return self._callConvCallback(CodeGen._CONV_TYPE_TO_LLVM, type, [type])

    def llvmTypeConvertToC(self, type):
        """Converts a StorageType into the LLVM type used for passing to C
        functions. 
        
        type: ~~StorageType - The type to converts.
        
        Returns: llvm.core.Type - The corresponding LLVM type for passing to C functions
        """ 
        return self._callConvCallback(CodeGen._CONV_TYPE_TO_LLVM, type, [type], must_find=False)
        if ll:
            return ll
        
        # Fall back.
        return self.llvmTypeConvert(type)
    
    ### Decorators.

    def convertConstToLLVM(self, type):
        """Decorator to define a conversion from a ConstOperand to the
        corresponding LLVM constant. The decorated function will receive two
        parameters *op* and *cast_to*. The former is the instance of
        ~~ConstOperand to be converted; the latter is a ~~Type to which the
        operand's value should be converted to first. If a conversion function
        receives a *cast_to* type it can't handle, it must abort via an
        ``assert``.  The conversion function must return an `llvm.core.Value``
        and can use the current :meth:`builder` if it needs to perform any
        transformations on the operand. 
        
        type: ~~StorageType - The type for which the conversion is being defined.
        """
        def register(func):
            CodeGen._Conversions[CodeGen._CONV_CONST_TO_LLVM] += [(type, func)]
    
        return register
    
    def convertTypeToLLVM(self, type):
        """Decorator to define a conversion from a StorageType to the
        corresponding type used in LLVM code. The decorated function will
        receive a single parameter *type* being the instance of ~~Type to
        convert, and must return an ``llvm.core.Type``..
        
        type: ~~StorageType - The type for which the conversion is being defined.
        """
        def register(func):
            CodeGen._Conversions[CodeGen._CONV_TYPE_TO_LLVM] += [(type, func)]
    
        return register
    
    def convertTypeToC(self, type):
        """Decorator to define a conversion from a StorageType to the
        corresponding type used with C functions. The decorated function will
        receive a single parameter *type* being the instance of ~~Type to
        convert. If no such conversion is defined for a type, we fall back to
        the ~~convertTypeToLLVM decorator.
        
        type: ~~StorageType - The type for which the conversion is being defined.
        """
        def register(func):
            CodeGen._Conversions[CodeGen._CONV_TYPE_TO_LLVM_C] += [(type, func)]
    
        return register
    
codegen = CodeGen()







