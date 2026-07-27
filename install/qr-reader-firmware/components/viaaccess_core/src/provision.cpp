#include "viaaccess/provision.hpp"

#include <cctype>

#include "viaaccess/strings.hpp"

namespace viaaccess {
namespace {

constexpr const char* kClaimTokenPrefix = "clm_";

struct ParsedUrl {
  bool ok = false;
  std::string scheme;
  // Host including the port, as written in the URL.
  std::string authority;
  std::string query;
};

ParsedUrl ParseUrl(const std::string& raw) {
  ParsedUrl parsed;
  const std::size_t scheme_end = raw.find("://");
  if (scheme_end == std::string::npos || scheme_end == 0) {
    return parsed;
  }
  parsed.scheme = ToLower(raw.substr(0, scheme_end));

  const std::size_t authority_begin = scheme_end + 3;
  std::size_t authority_end = raw.size();
  for (std::size_t i = authority_begin; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c == '/' || c == '?' || c == '#') {
      authority_end = i;
      break;
    }
  }
  parsed.authority = raw.substr(authority_begin, authority_end - authority_begin);
  if (parsed.authority.empty()) {
    return parsed;
  }

  const std::size_t query_begin = raw.find('?', authority_end);
  if (query_begin != std::string::npos) {
    const std::size_t fragment = raw.find('#', query_begin);
    parsed.query = fragment == std::string::npos
                       ? raw.substr(query_begin + 1)
                       : raw.substr(query_begin + 1, fragment - query_begin - 1);
  }

  parsed.ok = true;
  return parsed;
}

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

std::string PercentDecode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out.push_back(' ');
      continue;
    }
    if (value[i] == '%' && i + 2 < value.size()) {
      const int hi = HexValue(value[i + 1]);
      const int lo = HexValue(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(value[i]);
  }
  return out;
}

std::string QueryParam(const std::string& query, const std::string& key) {
  std::size_t begin = 0;
  while (begin <= query.size()) {
    std::size_t end = query.find('&', begin);
    if (end == std::string::npos) {
      end = query.size();
    }
    const std::string pair = query.substr(begin, end - begin);
    const std::size_t eq = pair.find('=');
    if (eq != std::string::npos && pair.substr(0, eq) == key) {
      return PercentDecode(pair.substr(eq + 1));
    }
    if (end == query.size()) {
      break;
    }
    begin = end + 1;
  }
  return "";
}

// Hostname strips userinfo, the port and IPv6 brackets from an authority.
std::string Hostname(const std::string& authority) {
  std::string host = authority;
  const std::size_t at = host.rfind('@');
  if (at != std::string::npos) {
    host = host.substr(at + 1);
  }
  if (!host.empty() && host.front() == '[') {
    const std::size_t close = host.find(']');
    if (close == std::string::npos) {
      return "";
    }
    return ToLower(host.substr(1, close - 1));
  }
  const std::size_t colon = host.find(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  return ToLower(host);
}

bool IsLoopbackBaseUrl(const std::string& raw) {
  const ParsedUrl parsed = ParseUrl(raw);
  if (!parsed.ok) {
    return false;
  }
  const std::string host = Hostname(parsed.authority);
  return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

}  // namespace

ProvisionInput ParseProvisionInput(const std::string& raw,
                                   const std::string& fallback_identity_url) {
  ProvisionInput out;
  const std::string trimmed = Trim(raw);
  if (trimmed.empty()) {
    out.error = "Informe a URL ou o token de provisionamento.";
    return out;
  }

  if (StartsWith(trimmed, kClaimTokenPrefix)) {
    const std::string base = TrimTrailingSlashes(Trim(fallback_identity_url));
    if (base.empty()) {
      out.error = "Informe a URL do Identity ou cole a URL completa de provisionamento.";
      return out;
    }
    out.ok = true;
    out.identity_url = base;
    out.claim_token = trimmed;
    return out;
  }

  const ParsedUrl parsed = ParseUrl(trimmed);
  if (!parsed.ok) {
    out.error = "Token ou URL de provisionamento inválidos.";
    return out;
  }

  const std::string token = Trim(QueryParam(parsed.query, "t"));
  if (!StartsWith(token, kClaimTokenPrefix)) {
    out.error = "URL de provisionamento sem token clm_.";
    return out;
  }

  out.ok = true;
  out.identity_url = parsed.scheme + "://" + parsed.authority;
  out.claim_token = token;
  return out;
}

std::string PreferReachableIdentityURL(const std::string& used_for_claim,
                                       const std::string& from_api) {
  const std::string used = TrimTrailingSlashes(Trim(used_for_claim));
  const std::string api = TrimTrailingSlashes(Trim(from_api));
  if (api.empty()) {
    return used;
  }
  if (used.empty()) {
    return api;
  }
  if (IsLoopbackBaseUrl(api) && !IsLoopbackBaseUrl(used)) {
    return used;
  }
  return api;
}

}  // namespace viaaccess
