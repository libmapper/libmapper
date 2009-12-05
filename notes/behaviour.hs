
import IO

data SignalUnits = NA | Normalized | Boolean deriving (Show, Eq)

data SignalValue = SignalInt Int | SignalFloat Float | Empty deriving (Show)

toSignalFloat :: SignalValue -> SignalValue
toSignalFloat (SignalInt i) = SignalFloat (fromIntegral i)
toSignalFloat (SignalFloat f) = SignalFloat f
toSignalFloat Empty = Empty

instance Eq SignalValue where
    _ == Empty = False
    Empty == _ = False
    (SignalInt a) == (SignalInt b) = a == b
    (SignalFloat a) == (SignalFloat b) = a == b
    a == b = (toSignalFloat a) == (toSignalFloat b)

-- Type of signal is 'f' or 'i' with an optional array size.
data SignalType = SigTypeFloat (Maybe Int) | SigTypeInt (Maybe Int)
    deriving (Eq)

instance Show SignalType where
    show (SigTypeInt Nothing) = "i"
    show (SigTypeFloat Nothing) = "f"
    show (SigTypeInt (Just i)) = ("i["++(show i)++"]")
    show (SigTypeFloat (Just i)) = ("f["++(show i)++"]")

data Signal = Signal {
      sigName :: String,
      sigMin :: SignalValue,
      sigMax :: SignalValue,
      sigUnits :: SignalUnits,
      sigType :: SignalType
} deriving (Show, Eq)

-- A normalized signal is a float value not in an array with min=0 and max=1.
-- A boolean signal is an integer value 0 or 1, not in an array.
newSignal :: SignalUnits -> String -> Signal
newSignal u n
  | u == Normalized = Signal n (SignalFloat 0.0) (SignalFloat 1.0) u (SigTypeFloat Nothing)
  | u == Boolean    = Signal n (SignalInt 0) (SignalInt 1) u (SigTypeInt Nothing)

data Device = Controller {
      deviceRootName :: String,
      deviceOutputs :: [Signal]
    }
 | Synth {
      deviceRootName :: String,
      deviceInputs :: [Signal]
    }
 | Router {
      deviceRootName :: String,
      deviceInputs :: [Signal],
      deviceOutputs :: [Signal]
    } deriving (Show, Eq)

data MessageValue =
    MsgValString String
  | MsgValInt Int
  | MsgValFloat Float
  deriving (Eq)

instance Show MessageValue where
    show (MsgValString s) = s
    show (MsgValInt i)    = show i
    show (MsgValFloat f)  = show f

data Message = Message { msgPath :: String, msgArgs :: [MessageValue] }
  deriving (Show, Eq)

receive :: Message -> String
receive (Message path args) = "received " ++ path
  ++ (foldl1 (++) $ map (\x -> (++) " " (show x)) args)

dispatch :: Message -> String
dispatch (Message "/link" [MsgValString a, MsgValString b])
  = "linking " ++ a ++ " and " ++ b
dispatch (Message a b) = "Unknown message " ++ a ++ " " ++ (show b)

adc1 = newSignal Normalized "adc1"
pin2 = newSignal Boolean "pin2"
pin3 = Signal "pin2" (SignalFloat 0.0) Empty NA (SigTypeFloat (Just 2))

tstick = Controller "tstick" [adc1]
modal = Synth "modal" [pin2, pin3]

main :: IO ()
main = do
  putStrLn $ show $ receive $ Message "/link" [(MsgValString "/tstick/1"),
                                               (MsgValString "/modal/1")]
  putStrLn $ show $ dispatch $ Message "/link" [(MsgValString "/tstick/1"),
                                                (MsgValString "/modal/1")]
  putStrLn $ show $ dispatch $ Message "/link" [(MsgValString "/tstick/1"),
                                                (MsgValString "/modal/1"),
                                                (MsgValInt 2)]
