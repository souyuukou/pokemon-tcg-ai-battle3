import ctypes
import os
import platform
    
class StartData(ctypes.Structure):
    _fields_ = [
        ("battlePtr", ctypes.c_void_p),
        ("errorPlayer", ctypes.c_int),
        ("errorType", ctypes.c_int),
    ]

class SerialData(ctypes.Structure):
    _fields_ = [
        ("json", ctypes.c_char_p),
        ("data", ctypes.POINTER(ctypes.c_ubyte)),
        ("count", ctypes.c_int),
        ("selectPlayer", ctypes.c_int)
    ]

os_name = platform.system()
if os_name == 'Windows':
    lib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cg.dll")
elif os_name == "Darwin":
    lib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "libcg.dylib")
elif platform.machine() in ('arm64', 'aarch64'):
    lib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "libcg-arm64.so")
else:
    lib_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "libcg.so")
lib = ctypes.cdll.LoadLibrary(lib_path)

lib.GameInitialize()

lib.BattleStart.restype = StartData
lib.BattleStart.argtypes = [ctypes.POINTER(ctypes.c_int)]

if hasattr(lib, "BattleStartSeeded"):
    lib.BattleStartSeeded.restype = StartData
    lib.BattleStartSeeded.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint]

lib.AgentStart.restype = ctypes.c_void_p

lib.BattleFinish.argtypes = [ctypes.c_void_p]

lib.GetBattleData.restype = SerialData
lib.GetBattleData.argtypes = [ctypes.c_void_p]

lib.Select.restype = ctypes.c_int
lib.Select.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_int]

lib.VisualizeData.restype = ctypes.c_char_p
lib.VisualizeData.argtypes = [ctypes.c_void_p]

lib.SearchBegin.restype = ctypes.c_char_p
lib.SearchBegin.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.c_int]

lib.SearchStep.restype = ctypes.c_char_p
lib.SearchStep.argtypes = [ctypes.c_void_p, ctypes.c_int64, ctypes.POINTER(ctypes.c_int), ctypes.c_int]

lib.SearchEnd.argtypes = [ctypes.c_void_p]

lib.SearchRelease.argtypes = [ctypes.c_void_p, ctypes.c_int64]

if hasattr(lib, "ExactDecide"):
    lib.ExactDecide.restype = ctypes.c_char_p
    lib.ExactDecide.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
        ctypes.c_int, ctypes.c_int]

if hasattr(lib, "ExactEvaluateAction"):
    lib.ExactEvaluateAction.restype = ctypes.c_char_p
    lib.ExactEvaluateAction.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
        ctypes.c_int, ctypes.c_int, ctypes.c_int]

if hasattr(lib, "ExactDecideV2"):
    lib.ExactDecideV2.restype = ctypes.c_char_p
    lib.ExactDecideV2.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int]

if hasattr(lib, "ExactTurnBegin"):
    lib.ExactTurnBegin.restype = ctypes.c_char_p
    lib.ExactTurnBegin.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.c_int, ctypes.c_int]

if hasattr(lib, "ExactTurnAdvance"):
    lib.ExactTurnAdvance.restype = ctypes.c_char_p
    lib.ExactTurnAdvance.argtypes = [
        ctypes.c_void_p, ctypes.c_int64, ctypes.c_char_p, ctypes.c_int, ctypes.c_int]

if hasattr(lib, "ExactTurnRelease"):
    lib.ExactTurnRelease.argtypes = [ctypes.c_void_p, ctypes.c_int64]

lib.AllCard.restype = ctypes.c_char_p

lib.AllAttack.restype = ctypes.c_char_p

class Battle:
    battle_ptr = None
    obs = None
