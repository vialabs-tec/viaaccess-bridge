#include "viaaccess/contingency.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "viaaccess/crypto.hpp"
#include "viaaccess/strings.hpp"

namespace viaaccess {
namespace {

std::string UrlDecode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size() &&
        std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
      const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
          return c - '0';
        }
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return 10 + (c - 'a');
      };
      out.push_back(static_cast<char>((hex(value[i + 1]) << 4) | hex(value[i + 2])));
      i += 2;
    } else if (value[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

std::string QueryParam(const std::string& query, const std::string& key) {
  std::size_t start = 0;
  while (start < query.size()) {
    std::size_t amp = query.find('&', start);
    if (amp == std::string::npos) {
      amp = query.size();
    }
    const std::string pair = query.substr(start, amp - start);
    const std::size_t eq = pair.find('=');
    const std::string name = eq == std::string::npos ? pair : pair.substr(0, eq);
    if (UrlDecode(name) == key) {
      return eq == std::string::npos ? "" : UrlDecode(pair.substr(eq + 1));
    }
    start = amp + 1;
  }
  return "";
}

std::string JsonStringField(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = json.find(needle);
  while (pos != std::string::npos) {
    std::size_t cursor = pos + needle.size();
    while (cursor < json.size() &&
           std::isspace(static_cast<unsigned char>(json[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != ':') {
      pos = json.find(needle, pos + 1);
      continue;
    }
    ++cursor;
    while (cursor < json.size() &&
           std::isspace(static_cast<unsigned char>(json[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != '"') {
      return "";
    }
    ++cursor;
    std::string value;
    while (cursor < json.size() && json[cursor] != '"') {
      if (json[cursor] == '\\' && cursor + 1 < json.size()) {
        value.push_back(json[cursor + 1]);
        cursor += 2;
        continue;
      }
      value.push_back(json[cursor]);
      ++cursor;
    }
    return value;
  }
  return "";
}

bool JsonNumberField(const std::string& json, const std::string& key, int64_t* out) {
  const std::string needle = "\"" + key + "\"";
  std::size_t pos = json.find(needle);
  while (pos != std::string::npos) {
    std::size_t cursor = pos + needle.size();
    while (cursor < json.size() &&
           std::isspace(static_cast<unsigned char>(json[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= json.size() || json[cursor] != ':') {
      pos = json.find(needle, pos + 1);
      continue;
    }
    ++cursor;
    while (cursor < json.size() &&
           std::isspace(static_cast<unsigned char>(json[cursor])) != 0) {
      ++cursor;
    }
    if (cursor >= json.size()) {
      return false;
    }
    char* end = nullptr;
    const long long value = std::strtoll(json.c_str() + cursor, &end, 10);
    if (end == json.c_str() + cursor) {
      return false;
    }
    *out = static_cast<int64_t>(value);
    return true;
  }
  return false;
}

VerifyResult Fail(const std::string& code, const std::string& error) {
  VerifyResult result;
  result.ok = false;
  result.code = code;
  result.error = error;
  return result;
}

struct PassageClaims {
  std::string subject;
  std::string intent;
  std::string ap;
  std::string gv;
  std::string issuer;
  int64_t expires_at = 0;
  bool has_exp = false;
};

bool VerifyTicketJWT(const std::string& token, const PolicyState& policy,
                     PassageClaims* claims, std::string* error) {
  const std::size_t first = token.find('.');
  const std::size_t second = first == std::string::npos ? std::string::npos
                                                       : token.find('.', first + 1);
  if (first == std::string::npos || second == std::string::npos ||
      token.find('.', second + 1) != std::string::npos) {
    *error = "ticket inválido: formato";
    return false;
  }

  const std::string header_b64 = token.substr(0, first);
  const std::string payload_b64 = token.substr(first + 1, second - first - 1);
  const std::string signature_b64 = token.substr(second + 1);
  const std::string signing_input = token.substr(0, second);

  std::vector<uint8_t> header_raw;
  std::vector<uint8_t> payload_raw;
  std::vector<uint8_t> signature;
  if (!Base64UrlDecode(header_b64, &header_raw) ||
      !Base64UrlDecode(payload_b64, &payload_raw) ||
      !Base64UrlDecode(signature_b64, &signature)) {
    *error = "ticket inválido: encoding";
    return false;
  }

  const std::string header(header_raw.begin(), header_raw.end());
  const std::string alg = ToLower(JsonStringField(header, "alg"));
  if (ToLower(policy.ticket_verify.alg) != alg) {
    *error = "ticket inválido: algoritmo inesperado: " + alg;
    return false;
  }

  std::vector<uint8_t> key;
  if (!Base64UrlDecode(policy.ticket_verify.key_b64, &key) || key.empty()) {
    *error = "chave de ticket inválida";
    return false;
  }

  const std::vector<uint8_t> expected = HmacSha256(key, signing_input);
  if (!ConstantTimeEqual(expected.data(), expected.size(), signature.data(),
                         signature.size())) {
    *error = "ticket inválido: assinatura";
    return false;
  }

  const std::string payload(payload_raw.begin(), payload_raw.end());
  claims->subject = JsonStringField(payload, "sub");
  claims->intent = JsonStringField(payload, "intent");
  claims->ap = JsonStringField(payload, "ap");
  claims->gv = JsonStringField(payload, "gv");
  claims->issuer = JsonStringField(payload, "iss");
  claims->has_exp = JsonNumberField(payload, "exp", &claims->expires_at);

  if (claims->subject.empty()) {
    *error = "ticket sem membro";
    return false;
  }
  if (claims->issuer != policy.ticket_verify.issuer) {
    *error = "ticket inválido: emissor";
    return false;
  }
  return true;
}

}  // namespace

bool ParseQR(const std::string& qr_url, ParsedQR* out) {
  if (out == nullptr) {
    return false;
  }
  const std::string trimmed = Trim(qr_url);
  if (trimmed.empty()) {
    return false;
  }

  std::size_t path_start = 0;
  const std::size_t scheme = trimmed.find("://");
  if (scheme != std::string::npos) {
    path_start = trimmed.find('/', scheme + 3);
    if (path_start == std::string::npos) {
      return false;
    }
  } else if (!trimmed.empty() && trimmed[0] == '/') {
    path_start = 0;
  } else {
    // Relative path without leading slash still accepted if it contains /r/.
    path_start = 0;
  }

  const std::size_t query_pos = trimmed.find('?', path_start);
  const std::string path =
      trimmed.substr(path_start, query_pos == std::string::npos
                                     ? std::string::npos
                                     : query_pos - path_start);
  const std::string query =
      query_pos == std::string::npos ? "" : trimmed.substr(query_pos + 1);

  const std::size_t marker = path.find("/r/");
  std::size_t intent_begin = std::string::npos;
  if (marker != std::string::npos) {
    intent_begin = marker + 3;
  } else if (StartsWith(path, "r/")) {
    intent_begin = 2;
  } else if (path == "r" || EndsWith(path, "/r")) {
    return false;
  }
  if (intent_begin == std::string::npos) {
    return false;
  }

  std::size_t intent_end = path.find('/', intent_begin);
  if (intent_end == std::string::npos) {
    intent_end = path.size();
  }
  const std::string intent_id = UrlDecode(path.substr(intent_begin, intent_end - intent_begin));
  const std::string signed_ticket = Trim(QueryParam(query, "st"));
  if (intent_id.empty() || signed_ticket.empty()) {
    return false;
  }
  out->intent_id = intent_id;
  out->signed_ticket = signed_ticket;
  return true;
}

bool MemoryNonceStore::IsConsumed(const std::string& intent_id) const {
  return consumed_.find(intent_id) != consumed_.end();
}

bool MemoryNonceStore::MarkConsumed(const std::string& intent_id, int64_t now_unix) {
  consumed_[intent_id] = now_unix;
  Prune(now_unix);
  return true;
}

void MemoryNonceStore::Prune(int64_t now_unix) {
  const int64_t cutoff = now_unix - kRetentionSeconds;
  for (auto it = consumed_.begin(); it != consumed_.end();) {
    if (it->second < cutoff) {
      it = consumed_.erase(it);
    } else {
      ++it;
    }
  }
}

void MemoryNonceStore::Clear() { consumed_.clear(); }

void MemoryNonceStore::Load(std::unordered_map<std::string, int64_t> consumed) {
  consumed_ = std::move(consumed);
}

bool PolicyHasMember(const PolicyState& policy, const std::string& member_id) {
  return std::find(policy.member_ids.begin(), policy.member_ids.end(), member_id) !=
         policy.member_ids.end();
}

bool PolicyTicketVerifyReady(const PolicyState& policy) {
  return policy.ticket_verify.alg == "HS256" && !policy.ticket_verify.key_b64.empty() &&
         !policy.ticket_verify.issuer.empty();
}

VerifyResult Verify(const VerifyInput& input) {
  if (!PolicyTicketVerifyReady(input.policy)) {
    return Fail("TICKET_VERIFY_NOT_CONFIGURED",
                "Snapshot sem chave de verificação. Aguarde sync de política.");
  }

  ParsedQR parsed;
  if (!ParseQR(input.qr_url, &parsed)) {
    return Fail("INVALID_QR", "QR sem ticket assinado (parâmetro st).");
  }

  if (input.nonce != nullptr && input.nonce->IsConsumed(parsed.intent_id)) {
    return Fail("INTENT_CONSUMED", "Intent já utilizado neste leitor.");
  }

  PassageClaims claims;
  std::string jwt_error;
  if (!VerifyTicketJWT(parsed.signed_ticket, input.policy, &claims, &jwt_error)) {
    return Fail("INVALID_TICKET", jwt_error);
  }

  if (claims.intent != parsed.intent_id) {
    return Fail("INTENT_MISMATCH", "Intent do ticket não confere com a URL.");
  }
  if (!input.access_point_slug.empty() && claims.ap != input.access_point_slug) {
    return Fail("ACCESS_POINT_MISMATCH", "Ticket não é para este ponto de acesso.");
  }
  if (!input.policy.grant_version.empty() && claims.gv != input.policy.grant_version) {
    return Fail("GRANT_VERSION_MISMATCH",
                "Política local desatualizada para este ticket.");
  }
  if (!PolicyHasMember(input.policy, claims.subject)) {
    return Fail("GRANT_DENIED", "Membro sem permissão no snapshot local.");
  }

  const int64_t now = input.now;
  if (claims.has_exp && now > claims.expires_at) {
    return Fail("INTENT_EXPIRED", "Ticket expirado.");
  }

  if (input.nonce != nullptr && !input.nonce->MarkConsumed(parsed.intent_id, now)) {
    return Fail("NONCE_STORE_ERROR", "Falha ao registrar intent consumido.");
  }

  VerifyResult result;
  result.ok = true;
  result.member_id = claims.subject;
  result.intent_id = parsed.intent_id;
  return result;
}

std::string SignPassageTicketHS256(const std::vector<uint8_t>& key,
                                   const std::string& subject,
                                   const std::string& intent_id,
                                   const std::string& access_point_slug,
                                   const std::string& grant_version,
                                   const std::string& issuer,
                                   int64_t expires_at_unix) {
  const std::string header = R"({"alg":"HS256","typ":"JWT"})";
  const std::string payload =
      std::string("{\"sub\":\"") + subject + "\",\"intent\":\"" + intent_id +
      "\",\"jti\":\"" + intent_id + "\",\"ap\":\"" + access_point_slug +
      "\",\"gv\":\"" + grant_version + "\",\"org\":\"org_1\",\"iss\":\"" + issuer +
      "\",\"exp\":" + std::to_string(expires_at_unix) + "}";
  const std::string header_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(header.data()), header.size());
  const std::string payload_b64 =
      Base64UrlEncode(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  const std::string signing_input = header_b64 + "." + payload_b64;
  const std::vector<uint8_t> signature = HmacSha256(key, signing_input);
  return signing_input + "." + Base64UrlEncode(signature);
}

}  // namespace viaaccess
