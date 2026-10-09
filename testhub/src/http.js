// Shared by the worker and the level plan: an error that becomes a JSON reply with its status.

export class HttpError extends Error {
  constructor(status, message) { super(message); this.status = status; }
}

export function json(body, status = 200, headers = {}) {
  return new Response(JSON.stringify(body), {
    status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store', ...headers },
  });
}

// A JSON request body. The content type is required: a cross-site form can post text/plain that happens to be
// JSON, but it cannot set application/json without a CORS preflight the site never answers.
// Bounded too: text is cleaned before it is cut to length, so a 100 MB post would otherwise cost the whole request.
export const MAX_JSON = 256 * 1024;

export async function body(request) {
  if (!/^application\/json\b/i.test(request.headers.get('content-type') || '')) throw new HttpError(415, 'application/json expected');
  if (Number(request.headers.get('content-length') || 0) > MAX_JSON) throw new HttpError(413, 'request too large');
  const text = await request.text();
  if (text.length > MAX_JSON) throw new HttpError(413, 'request too large');
  try { return JSON.parse(text); } catch { throw new HttpError(400, 'JSON body expected'); }
}
