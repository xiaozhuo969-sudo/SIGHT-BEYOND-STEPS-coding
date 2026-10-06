# Ghidra headless post-analysis script; output is reference pseudocode, not build input.
from ghidra.app.decompiler import DecompInterface
from ghidra.program.model.symbol import RefType
from ghidra.program.model.symbol import SourceType
from ghidra.program.model.data import FloatDataType, UnsignedIntegerDataType, VoidDataType
from ghidra.program.model.listing import ParameterImpl, Function
from ghidra.program.model.address import AddressSet
import json

root = getScriptArgs()[0]
for address, name, return_type, parameter_types in [
    (0x080012cc, "RCC_ADCCLKConfig", VoidDataType.dataType, [UnsignedIntegerDataType.dataType]),
    # The Keil binary passes float bit patterns in r0/r1 (soft-float ABI).
    # Keep uint32 types here: Ghidra's generic float ABI otherwise selects FP registers.
    (0x08001d20, "soft_float_add", UnsignedIntegerDataType.dataType, [UnsignedIntegerDataType.dataType, UnsignedIntegerDataType.dataType]),
    (0x08001de4, "soft_float_to_uint", UnsignedIntegerDataType.dataType, [UnsignedIntegerDataType.dataType]),
    (0x08001e24, "soft_uint_to_float", UnsignedIntegerDataType.dataType, [UnsignedIntegerDataType.dataType]),
    (0x08001e4c, "soft_float_multiply", UnsignedIntegerDataType.dataType, [UnsignedIntegerDataType.dataType, UnsignedIntegerDataType.dataType]),
]:
    f = getFunctionAt(toAddr(address))
    f.setName(name, SourceType.USER_DEFINED)
    f.setReturnType(return_type, SourceType.USER_DEFINED)
    parameters = [ParameterImpl("arg%d" % i, dt, currentProgram) for i, dt in enumerate(parameter_types)]
    f.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, True,
                        SourceType.USER_DEFINED, parameters)
decompiler = DecompInterface()
decompiler.openProgram(currentProgram)
records = []
out = open(root + "/decompiled_reference.c", "w")
out.write("/* Ghidra 11.0.3 ARM Cortex-M3 decompilation.\n"
          " * Addresses and inferred names retained for comparison.\n"
          " * This file is pseudocode; build the manually reconstructed C sources.\n"
          " * Source SHA256: cbe3d9ff29cbe4ed8b158dc277fd6280a572135573af9f70b42d96420f9ad43d\n */\n\n")
for f in currentProgram.getFunctionManager().getFunctions(True):
    if f.getEntryPoint().getOffset() >= 0x080020ce:
        continue
    result = decompiler.decompileFunction(f, 30, monitor)
    calls = sorted([str(x.getEntryPoint()) + " " + x.getName() for x in f.getCalledFunctions(monitor)])
    rec = {"address": str(f.getEntryPoint()), "name": f.getName(),
           "bytes": f.getBody().getNumAddresses(), "calls": calls,
           "decompiled": result.decompileCompleted()}
    records.append(rec)
    out.write("\n/* %s | %s | %d bytes */\n" % (rec["address"], rec["name"], rec["bytes"]))
    if result.decompileCompleted():
        out.write(result.getDecompiledFunction().getC())
    else:
        out.write("/* Decompilation failed: %s */\n" % result.getErrorMessage())
out.close()
open(root + "/function_map.json", "w").write(json.dumps(records, indent=2))
decompiler.dispose()
listing_out = open(root + "/firmware_disassembly.txt", "w")
listing_out.write("# Original Cortex-M3 firmware: instructions discovered by Ghidra.\n"
                  "# Literal pools/vector/data are omitted; C runtime inline dispatch may be incomplete.\n")
address_set = AddressSet(toAddr(0x080000ec), toAddr(0x080020cd))
for inst in currentProgram.getListing().getInstructions(address_set, True):
    f = getFunctionAt(inst.getAddress())
    if f is not None:
        listing_out.write("\n# %s: %s\n" % (str(f.getEntryPoint()), f.getName()))
    raw = " ".join("%02x" % (int(b) & 255) for b in inst.getBytes())
    operands = ", ".join(inst.getDefaultOperandRepresentation(i) for i in range(inst.getNumOperands()))
    listing_out.write("%s\t%-12s\t%s\t%s\n" % (str(inst.getAddress()), raw, inst.getMnemonicString(), operands))
listing_out.close()
println("Exported %d functions; %d successful decompilations" % (len(records), sum(x["decompiled"] for x in records)))
