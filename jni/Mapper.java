
class Mapper
{
    native double t(int i, String s); 

    static { 
        System.loadLibrary("mapperjni-0");
    } 
}
