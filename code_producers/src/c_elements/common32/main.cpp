#include <iostream>
#include <sstream>
#include <cstring>
#include <exception>
#include <algorithm>
#include <system_error>
#include <stdexcept>
#include <cstdlib>
#include <utility>
#include <vector>

#ifndef CIRCOM_LINKED_WITNESS_ONLY
#include <fstream>
#include <iomanip>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <chrono>
using json = nlohmann::json;
#endif

#if defined(_WIN32)
#define CIRCOM_LINKED_EXPORT __declspec(dllexport)
#else
#define CIRCOM_LINKED_EXPORT __attribute__((visibility("default")))
#endif

#include "calcwit.hpp"
#include "circom.hpp"


static void copyError(u8 *error_msg, size_t error_msg_len, const std::string &message) {
    if (error_msg == nullptr || error_msg_len == 0) return;
    size_t copy_len = std::min(message.size(), error_msg_len - 1);
    memcpy(error_msg, message.data(), copy_len);
    error_msg[copy_len] = 0;
}

#define handle_error(msg) \
           do { perror(msg); exit(EXIT_FAILURE); } while (0)

static Circom_Circuit* loadCircuitFromBytes(const u8 *bdata, size_t data_size) {
    Circom_Circuit *circuit = new Circom_Circuit;

    circuit->InputHashMap = new HashSignalInfo[get_size_of_input_hashmap()];
    uint dsize = get_size_of_input_hashmap()*sizeof(HashSignalInfo);
    memcpy((void *)(circuit->InputHashMap), (void *)bdata, dsize);

    circuit->witness2SignalList = new u64[get_size_of_witness()];
    uint inisize = dsize;    
    dsize = get_size_of_witness()*sizeof(u64);
    memcpy((void *)(circuit->witness2SignalList), (void *)(bdata+inisize), dsize);

    std::map<u32,IOFieldDefPair> templateInsId2IOSignalInfo1;
    IOFieldDefPair* busInsId2FieldInfo1 = nullptr;
    if (get_size_of_io_map()>0) {
      std::vector<u32> index(get_size_of_io_map());
      inisize += dsize;
      dsize = get_size_of_io_map()*sizeof(u32);
      memcpy((void *)index.data(), (void *)(bdata+inisize), dsize);
      inisize += dsize;
      assert(inisize % sizeof(u32) == 0);    
      assert(data_size % sizeof(u32) == 0);
      if (inisize > data_size) {
        throw std::runtime_error("invalid circuit data layout");
      }
      std::vector<u32> dataiomap((data_size-inisize)/sizeof(u32));
      memcpy((void *)dataiomap.data(), (void *)(bdata+inisize), data_size-inisize);
      u32* pu32 = dataiomap.data();
      for (int i = 0; i < get_size_of_io_map(); i++) {
	u32 n = *pu32;
	IOFieldDefPair p;
	p.len = n;
	std::vector<IOFieldDef> defs(n);
	pu32 += 1;
	for (u32 j = 0; j <n; j++){
	  defs[j].offset=*pu32;
	  u32 len = *(pu32+1);
	  defs[j].len = len;
	  defs[j].lengths = new u32[len];
	  memcpy((void *)defs[j].lengths,(void *)(pu32+2),len*sizeof(u32));
	  pu32 += len + 2;
	  defs[j].size=*pu32;
	  defs[j].busId=*(pu32+1);	  
	  pu32 += 2;
	}
	p.defs = (IOFieldDef*)calloc(p.len, sizeof(IOFieldDef));
	for (u32 j = 0; j < p.len; j++){
	  p.defs[j] = defs[j];
	}
	templateInsId2IOSignalInfo1[index[i]] = p;
      }
      busInsId2FieldInfo1 = (IOFieldDefPair*)calloc(get_size_of_bus_field_map(), sizeof(IOFieldDefPair));
      for (int i = 0; i < get_size_of_bus_field_map(); i++) {
	u32 n = *pu32;
	IOFieldDefPair p;
	p.len = n;
	std::vector<IOFieldDef> defs(n);
	pu32 += 1;
	for (u32 j = 0; j <n; j++){
	  defs[j].offset=*pu32;
	  u32 len = *(pu32+1);
	  defs[j].len = len;
	  defs[j].lengths = new u32[len];
	  memcpy((void *)defs[j].lengths,(void *)(pu32+2),len*sizeof(u32));
	  pu32 += len + 2;
	  defs[j].size=*pu32;
	  defs[j].busId=*(pu32+1);	  
	  pu32 += 2;
	}
	p.defs = (IOFieldDef*)calloc(p.len, sizeof(IOFieldDef));
	for (u32 j = 0; j < p.len; j++){
	  p.defs[j] = defs[j];
	}
	busInsId2FieldInfo1[i] = p;
      }
    }
    circuit->templateInsId2IOSignalInfo = std::move(templateInsId2IOSignalInfo1);
    circuit->busInsId2FieldInfo = busInsId2FieldInfo1;

    return circuit;
}

static void freeIOFieldDefPair(IOFieldDefPair &pair) {
    for (u32 i = 0; i < pair.len; i++) {
        delete[] pair.defs[i].lengths;
    }
    free(pair.defs);
    pair.defs = nullptr;
    pair.len = 0;
}

static void freeCircuit(Circom_Circuit *circuit) {
    if (circuit == nullptr) return;
    delete[] circuit->InputHashMap;
    delete[] circuit->witness2SignalList;
    for (auto &entry : circuit->templateInsId2IOSignalInfo) {
        freeIOFieldDefPair(entry.second);
    }
    if (circuit->busInsId2FieldInfo != nullptr) {
        for (uint i = 0; i < get_size_of_bus_field_map(); i++) {
            freeIOFieldDefPair(circuit->busInsId2FieldInfo[i]);
        }
        free(circuit->busInsId2FieldInfo);
    }
    delete circuit;
}

#ifndef CIRCOM_LINKED_WITNESS_ONLY
Circom_Circuit* loadCircuit(std::string const &datFileName) {
    int fd;
    struct stat sb;

    fd = open(datFileName.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cout << ".dat file not found: " << datFileName << "\n";
        throw std::system_error(errno, std::generic_category(), "open");
    }

    if (fstat(fd, &sb) == -1) {          /* To obtain file size */
        throw std::system_error(errno, std::generic_category(), "fstat");
    }

    u8* bdata = (u8*)mmap(NULL, sb.st_size, PROT_READ , MAP_PRIVATE, fd, 0);
    close(fd);
    Circom_Circuit *circuit = loadCircuitFromBytes(bdata, sb.st_size);
    munmap(bdata, sb.st_size);
    
    return circuit;
}

bool check_valid_number(std::string & s, uint base){
  bool is_valid = true;
  if (base == 16){
    for (uint i = 0; i < s.size(); i++){
      is_valid &= (
        ('0' <= s[i] && s[i] <= '9') || 
        ('a' <= s[i] && s[i] <= 'f') ||
        ('A' <= s[i] && s[i] <= 'F')
      );
    }
  } else{
    for (uint i = 0; i < s.size(); i++){
      is_valid &= ('0' <= s[i] && s[i] < char(int('0') + base));
    }
  }
  return is_valid;
}

FrElement str2element(const char *s, uint base) {
  static mpz_t q;
  mpz_init_set_ui(q,{{prime}});
  mpz_t mr;
  mpz_init_set_str(mr, s, base);
  mpz_fdiv_r(mr, mr, q);
  FrElement v = (FrElement)mpz_get_ui(mr);
  mpz_clear(mr);
  return v;
}

void json2FrElements (json val, std::vector<FrElement> & vval){
  if (!val.is_array()) {
    FrElement v;
    std::string s_aux, s;
    uint base;
    if (val.is_string()) {
      s_aux = val.get<std::string>();
      std::string possible_prefix = s_aux.substr(0, 2);
      if (possible_prefix == "0b" || possible_prefix == "0B"){
        s = s_aux.substr(2, s_aux.size() - 2);
        base = 2; 
      } else if (possible_prefix == "0o" || possible_prefix == "0O"){
        s = s_aux.substr(2, s_aux.size() - 2);
        base = 8; 
      } else if (possible_prefix == "0x" || possible_prefix == "0X"){
        s = s_aux.substr(2, s_aux.size() - 2);
        base = 16;
      } else{
        s = s_aux;
        base = 10;
      }
      if (!check_valid_number(s, base)){
        std::ostringstream errStrStream;
        errStrStream << "Invalid number in JSON input: " << s_aux << "\n";
	      throw std::runtime_error(errStrStream.str() );
      }
    } else if (val.is_number()) {
        double vd = val.get<double>();
        std::stringstream stream;
        stream << std::fixed << std::setprecision(0) << vd;
        s = stream.str();
        base = 10;
    } else {
        std::ostringstream errStrStream;
        errStrStream << "Invalid JSON type\n";
	      throw std::runtime_error(errStrStream.str() );
    }
    vval.push_back(str2element(s.c_str(), base));
  } else {
    for (uint i = 0; i < val.size(); i++) {
      json2FrElements (val[i], vval);
    }
  }
}

json::value_t check_type(std::string prefix, json in){
  if (not in.is_array()) {
    if (in.is_number_integer() || in.is_number_unsigned() || in.is_string())
      return json::value_t::number_integer;
    else  return in.type();
    } else {
    if (in.size() == 0) return json::value_t::null;
    json::value_t t = check_type(prefix, in[0]);
    for (uint i = 1; i < in.size(); i++) {
      if (t != check_type(prefix, in[i])) {
	fprintf(stderr, "Types are not the same in the the key %s\n",prefix.c_str());
	assert(false);
      }
    }
    return t;
  }
}

void qualify_input(std::string prefix, json &in, json &in1);

void qualify_input_list(std::string prefix, json &in, json &in1){
    if (in.is_array()) {
      for (uint i = 0; i<in.size(); i++) {
	  std::string new_prefix = prefix + "[" + std::to_string(i) + "]";
	  qualify_input_list(new_prefix,in[i],in1);
	}
    } else {
	qualify_input(prefix,in,in1);
    }
}

void qualify_input(std::string prefix, json &in, json &in1) {
  if (in.is_array()) {
    if (in.size() > 0) {
      json::value_t t = check_type(prefix,in);
      if (t == json::value_t::object) {
	qualify_input_list(prefix,in,in1);
      } else {
	in1[prefix] = in;
      }
    } else {
      in1[prefix] = in;
    }
  } else if (in.is_object()) {
    for (json::iterator it = in.begin(); it != in.end(); ++it) {
      std::string new_prefix = prefix.length() == 0 ? it.key() : prefix + "." + it.key();
      qualify_input(new_prefix,it.value(),in1);
    }
  } else {
    in1[prefix] = in;
  }
}

void loadBinary(Circom_CalcWit *ctx, std::string filename) {
    int fd;
    struct stat sb;

    fd = open(filename.c_str(), O_RDONLY);
    if (fd == -1) {
        std::cout << ".dat file not found: " << filename << "\n";
        throw std::system_error(errno, std::generic_category(), "open");
    }
    
    if (fstat(fd, &sb) == -1) {          /* To obtain file size */
        throw std::system_error(errno, std::generic_category(), "fstat");
    }
    assert(sb.st_size / sizeof(FrElement) == get_main_input_signal_no());
    u8* bdata = (u8*)mmap(NULL, sb.st_size, PROT_READ , MAP_PRIVATE, fd, 0);
    close(fd);
    uint dsize = get_main_input_signal_no()*sizeof(FrElement);
    memcpy((void *)(ctx->signalValues+get_main_input_signal_start()), (void *)bdata, dsize);
}
#endif

void loadBinaryBuffer(Circom_CalcWit *ctx, const u8 *buffer, size_t buffer_size) {
    size_t expected_size = (size_t)get_main_input_signal_no() * sizeof(FrElement);
    if (buffer_size != expected_size) {
        std::ostringstream errStrStream;
        errStrStream << "Invalid binary input length: expected " << expected_size
                     << " bytes, got " << buffer_size << "\n";
        throw std::runtime_error(errStrStream.str());
    }
    memcpy((void *)(ctx->signalValues+get_main_input_signal_start()), (const void *)buffer, expected_size);
}

#ifndef CIRCOM_LINKED_WITNESS_ONLY
void loadJson(Circom_CalcWit *ctx, std::string filename) {
  std::ifstream inStream(filename);
  json jin;
  inStream >> jin;
  json j;

  //std::cout << jin << std::endl;
  std::string prefix = "";
  qualify_input(prefix, jin, j);
  //std::cout << j << std::endl;
  
  u64 nItems = j.size();
  // printf("Items : %llu\n",nItems);
  if (nItems == 0){
    ctx->tryRunCircuit();
  }
  for (json::iterator it = j.begin(); it != j.end(); ++it) {
    // std::cout << it.key() << " => " << it.value() << '\n';
    u64 h = fnv1a(it.key());
    std::vector<FrElement> v;
    json2FrElements(it.value(),v);
    uint signalSize = ctx->getInputSignalSize(h);
    if (v.size() < signalSize) {
	std::ostringstream errStrStream;
	errStrStream << "Error loading signal " << it.key() << ": Not enough values\n";
	throw std::runtime_error(errStrStream.str() );
    }
    if (v.size() > signalSize) {
	std::ostringstream errStrStream;
	errStrStream << "Error loading signal " << it.key() << ": Too many values\n";
	throw std::runtime_error(errStrStream.str() );
    }
    for (uint i = 0; i<v.size(); i++){
      try {
	// std::cout << it.key() << "," << i << " => " << Fr_element2str(&(v[i])) << '\n';
	ctx->setInputSignal(h,i,v[i]);
      } catch (std::runtime_error e) {
	std::ostringstream errStrStream;
	errStrStream << "Error setting signal: " << it.key() << "\n" << e.what();
	throw std::runtime_error(errStrStream.str() );
      }
    }
  }
}

void writeBinWitness(Circom_CalcWit *ctx, std::string wtnsFileName) {
    FILE *write_ptr;

    write_ptr = fopen(wtnsFileName.c_str(),"wb");

    fwrite("wtns", 4, 1, write_ptr);

    u32 version = 2;
    fwrite(&version, 4, 1, write_ptr);

    u32 nSections = 2;
    fwrite(&nSections, 4, 1, write_ptr);

    // Header
    u32 idSection1 = 1;
    fwrite(&idSection1, 4, 1, write_ptr);

    u32 n8 = 4;

    u64 idSection1length = 8 + n8;
    fwrite(&idSection1length, 8, 1, write_ptr);

    fwrite(&n8, 4, 1, write_ptr);

    // q is the prime
    FrElement q = {{prime}};
    fwrite(&q, 4, 1, write_ptr);

    uint Nwtns = get_size_of_witness();
    
    u32 nVars = (u32)Nwtns;
    fwrite(&nVars, 4, 1, write_ptr);

    // Data
    u32 idSection2 = 2;
    fwrite(&idSection2, 4, 1, write_ptr);
    
    u64 idSection2length = (u64)n8*(u64)Nwtns;
    fwrite(&idSection2length, 8, 1, write_ptr);

    FrElement *v = new FrElement[Nwtns];
    for (int i=0;i<Nwtns;i++) {
        ctx->getWitness(i, v[i]);
    }
    fwrite(v, 4, Nwtns, write_ptr);

    fclose(write_ptr);
}
#endif

extern "C" CIRCOM_LINKED_EXPORT void* {{run_name}}_load_circuit(
    const u8 *circuit_buffer,
    size_t circuit_size,
    u8 *error_msg,
    size_t error_msg_len
) {
    try {
        return (void*)loadCircuitFromBytes(circuit_buffer, circuit_size);
    } catch (const std::exception &e) {
        copyError(error_msg, error_msg_len, e.what());
        return nullptr;
    } catch (...) {
        copyError(error_msg, error_msg_len, "unknown circuit load error");
        return nullptr;
    }
}

extern "C" CIRCOM_LINKED_EXPORT void {{run_name}}_free_circuit(void *circuit_handle) {
    freeCircuit((Circom_Circuit*)circuit_handle);
}

extern "C" CIRCOM_LINKED_EXPORT int {{run_name}}_linked_witness(
    void *circuit_handle,
    const u8 *input_buffer,
    size_t input_size,
    u32 *witness,
    size_t witness_len,
    u32 *public_inputs,
    size_t public_inputs_len,
    u8 *error_msg,
    size_t error_msg_len
) {
    try {
        Circom_Circuit *circuit = (Circom_Circuit*)circuit_handle;
        if (circuit == nullptr) {
            throw std::runtime_error("null circuit handle");
        }
        size_t total_witness = (size_t)get_size_of_witness();
        if (public_inputs_len + witness_len + 1 != total_witness) {
            std::ostringstream errStrStream;
            errStrStream << "Invalid output layout: expected "
                         << (total_witness - 1) << " non-constant values, got "
                         << (public_inputs_len + witness_len) << "\n";
            throw std::runtime_error(errStrStream.str());
        }
        Circom_CalcWit ctx(circuit);
        loadBinaryBuffer(&ctx, input_buffer, input_size);
        ctx.runCircuit();

        FrElement value;
        for (size_t i = 0; i < public_inputs_len; i++) {
            ctx.getWitness((uint)(1 + i), value);
            public_inputs[i] = value;
        }
        for (size_t i = 0; i < witness_len; i++) {
            ctx.getWitness((uint)(1 + public_inputs_len + i), value);
            witness[i] = value;
        }
        return 0;
    } catch (const std::exception &e) {
        copyError(error_msg, error_msg_len, e.what());
        return 1;
    } catch (...) {
        copyError(error_msg, error_msg_len, "unknown witness generator error");
        return 1;
    }
}

#ifndef CIRCOM_LINKED_WITNESS_ONLY
int main (int argc, char *argv[]) {
  std::string cl(argv[0]);
  if (argc!=3) {
        std::cout << "Usage: " << cl << " <input.json> <output.wtns>\n";
  } else {
    std::string datfile = cl + ".dat";
    std::string inputfile(argv[1]);
    std::string wtnsfile(argv[2]);
  
    // auto t_start = std::chrono::high_resolution_clock::now();

   Circom_Circuit *circuit = loadCircuit(datfile);

   Circom_CalcWit *ctx = new Circom_CalcWit(circuit);
  
    if (inputfile.substr(inputfile.find_last_of(".") + 1) == "json") { 
      loadJson(ctx, inputfile);
      if (ctx->getRemaingInputsToBeSet()!=0) {
        std::cerr << "Not all inputs have been set. Only " << get_main_input_signal_no()-ctx->getRemaingInputsToBeSet() << " out of " << get_main_input_signal_no() << std::endl;
        assert(false);
      }
    } else {
      loadBinary(ctx, inputfile);
      ctx->runCircuit();
   }
   /*
     for (uint i = 0; i<get_size_of_witness(); i++){
     FrElement x;
     ctx->getWitness(i, x);
     std::cout << i << ": " << x << std::endl;
     }
   */
  
   //auto t_mid = std::chrono::high_resolution_clock::now();
   //std::cout << std::chrono::duration<double, std::milli>(t_mid-t_start).count()<<std::endl;

   writeBinWitness(ctx,wtnsfile);
  
   //auto t_end = std::chrono::high_resolution_clock::now();
   //std::cout << std::chrono::duration<double, std::milli>(t_end-t_mid).count()<<std::endl;

  }  
}
#endif
