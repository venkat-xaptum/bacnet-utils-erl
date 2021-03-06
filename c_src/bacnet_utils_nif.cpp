#include <cstring>

#include "erl_nif.h"
#include "bacnet_utils.hpp"

#define OK "ok"
#define BUFFER_SIZE 1500

ERL_NIF_TERM
mk_atom(ErlNifEnv* env, const char* atom)
{
    ERL_NIF_TERM ret;

    if(!enif_make_existing_atom(env, atom, &ret, ERL_NIF_LATIN1))
    {
        return enif_make_atom(env, atom);
    }

    return ret;
}

ERL_NIF_TERM
mk_error(ErlNifEnv* env, const char* mesg)
{
    return enif_make_tuple2(env, mk_atom(env, "error"), mk_atom(env, mesg));
}

static ERL_NIF_TERM
build_write_property_request_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifUInt64 id, tag;
  
  if(
     argc != 2 ||
     !enif_get_uint64(env, argv[0], &id) ||
     !enif_get_uint64(env, argv[1], &tag)
     ) {
    return enif_make_badarg(env);
  }

  uint8_t write_buffer[BUFFER_SIZE];
  uint8_t tmp_buffer[BUFFER_SIZE];
  
  BACNET_APPLICATION_DATA_VALUE bacnet_payload[1];
  build_bacnet_payload(bacnet_payload[0], id, tag);
  uint8_t wp_len = build_write_property_request(write_buffer, tmp_buffer, bacnet_payload);

  if( !wp_len ) {
    return mk_error(env, "write_property_request_error");
  }

  ErlNifBinary wp;
  if (!enif_alloc_binary(wp_len, &wp)) {
    return mk_error(env, "alloc_failed");
  }

  // Copy data to the ErlNifBinary
  std::memcpy(wp.data, write_buffer, wp_len);

  // return tuple
  ERL_NIF_TERM ok = mk_atom(env, OK);
  ERL_NIF_TERM wpBin = enif_make_binary(env, &wp);

  return enif_make_tuple2(env, ok, wpBin);
}

static ERL_NIF_TERM
build_read_property_request_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  if(argc != 0) {
    return enif_make_badarg(env);
  }

  uint8_t buffer[BUFFER_SIZE];
  uint8_t tmp_buffer[BUFFER_SIZE];

  uint8_t rp_len = build_read_property_request(buffer, tmp_buffer);
  if( !rp_len ) {
    return mk_error(env, "read_property_request_error");
  }

  ErlNifBinary rp;
  if (!enif_alloc_binary(rp_len, &rp)) {
    return mk_error(env, "alloc_failed");
  }

  // Copy data to the ErlNifBinary
  std::memcpy(rp.data, buffer, rp_len);

  // return tuple
  ERL_NIF_TERM ok = mk_atom(env, OK);
  ERL_NIF_TERM rpBin = enif_make_binary(env, &rp);

  return enif_make_tuple2(env, ok, rpBin);
}

static ERL_NIF_TERM
get_apdu_from_message_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary msg;
  
  if(
     argc != 1 ||
     !enif_inspect_binary(env, argv[0], &msg)
     ) {
    return enif_make_badarg(env);
  }

  uint8_t* apdu;
  uint16_t apdu_len;

  uint8_t * buffer = reinterpret_cast<uint8_t *>(msg.data);
  if (!get_apdu_from_message(buffer, &apdu, &apdu_len)) {
    return mk_error(env, "apdu_parse_error");
  }

  ErlNifBinary apduBin;
  if (!enif_alloc_binary(apdu_len, &apduBin)) {
    return mk_error(env, "alloc_failed");
  }

  // Copy data to the ErlNifBinary
  std::memcpy(apduBin.data, apdu, apdu_len);

  // return tuple
  ERL_NIF_TERM ok = mk_atom(env, OK);
  ERL_NIF_TERM apduBinTerm = enif_make_binary(env, &apduBin);

  return enif_make_tuple2(env, ok, apduBinTerm);
}

static ERL_NIF_TERM
get_pdu_type_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
  ErlNifBinary apduBin;
  
  if(
     argc != 1 ||
     !enif_inspect_binary(env, argv[0], &apduBin)
     ) {
    return enif_make_badarg(env);
  }

  uint8_t* apdu =  reinterpret_cast<uint8_t *>(apduBin.data);

  switch (get_pdu_type(apdu)) {
  case PDU_TYPE_SIMPLE_ACK:
    return mk_atom(env, "pdu_type_simple_ack");
    
  case PDU_TYPE_COMPLEX_ACK:
    return mk_atom(env, "pdu_type_complex_ack");

  default:
    return mk_error(env, "pdu_type_not_recognized");
  }
}

static ERL_NIF_TERM
get_value_from_complex_ack_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    ErlNifBinary apduBin;
  
  if(
     argc != 1 ||
     !enif_inspect_binary(env, argv[0], &apduBin)
     ) {
    return enif_make_badarg(env);
  }

  uint8_t* apdu =  reinterpret_cast<uint8_t *>(apduBin.data);
  uint16_t apdu_len = apduBin.size;
  

  BACNET_APPLICATION_DATA_VALUE value;
  bool value_success = get_value_from_complex_ack(apdu, apdu_len, value);
  if (!value_success) {
    return mk_error(env, "apdu_value_error");
  }

  if (value.tag != BACNET_APPLICATION_TAG_OCTET_STRING) {
    return mk_error(env, "invalid_octet_string");
  }
  
  // get octet string in the bacnet packet
  uint8_t* received_value = value.type.Octet_String.value;
  std::size_t value_length = value.type.Octet_String.length;

  // parse the octet string to get id and tag
  uint64_t value_id;
  uint64_t value_tag;
  std::size_t expected_length = sizeof(value_id) + sizeof(value_tag);

  if (value_length < expected_length) {
    return mk_error(env, "apdu_value_length_error");
  }
      
  std::memcpy(&value_id, received_value, sizeof(value_id));
  std::memcpy(&value_tag, received_value + sizeof(value_id), sizeof(value_tag));
       
  ERL_NIF_TERM ok =  mk_atom(env, OK);
  ERL_NIF_TERM id = enif_make_uint64(env, value_id);
  ERL_NIF_TERM tag = enif_make_uint64(env, value_tag);

  return enif_make_tuple3(env, ok, id, tag);
}

static ErlNifFunc nif_funcs[] = {
  {"build_write_property_request_nif", 2, build_write_property_request_nif},
  {"build_read_property_request_nif", 0, build_read_property_request_nif},
  {"get_apdu_from_message_nif",1,get_apdu_from_message_nif},
  {"get_pdu_type_nif", 1, get_pdu_type_nif},
  {"get_value_from_complex_ack_nif", 1, get_value_from_complex_ack_nif}
};

ERL_NIF_INIT(bacnet_utils, nif_funcs, NULL, NULL, NULL, NULL);


