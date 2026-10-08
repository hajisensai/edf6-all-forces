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
export async function body(request) {
  if (!/^application\/json\b/i.test(request.headers.get('content-type') || '')) throw new HttpError(415, 'application/json expected');
  try { return await request.json(); } catch { throw new HttpError(400, 'JSON body expected'); }
}
