const http = require("http");

const LISTEN_HOST = "0.0.0.0";
const LISTEN_PORT = 13337;
const TARGET_HOST = "WIN-7NSUSSD9RAB.local";
const TARGET_PORT = 13338;

const server = http.createServer((clientReq, clientRes) => {
  const headers = { ...clientReq.headers, host: `${TARGET_HOST}:${TARGET_PORT}` };
  const options = {
    hostname: TARGET_HOST,
    port: TARGET_PORT,
    path: clientReq.url,
    method: clientReq.method,
    headers,
  };

  const proxyReq = http.request(options, (proxyRes) => {
    clientRes.writeHead(proxyRes.statusCode, proxyRes.headers);
    proxyRes.pipe(clientRes);
  });

  proxyReq.on("error", (err) => {
    console.error(`Proxy error: ${err.message}`);
    clientRes.writeHead(502);
    clientRes.end("Bad Gateway");
  });

  clientReq.pipe(proxyReq);
});

server.on("error", (err) => {
  console.error(`Server error: ${err.message}`);
});

process.on("uncaughtException", (err) => {
  console.error(`Uncaught exception: ${err.message}`);
});

process.on("unhandledRejection", (reason) => {
  console.error(`Unhandled rejection: ${reason}`);
});

server.listen(LISTEN_PORT, LISTEN_HOST, () => {
  console.log(`Reverse proxy listening on ${LISTEN_HOST}:${LISTEN_PORT} -> http://${TARGET_HOST}:${TARGET_PORT}/`);
});
