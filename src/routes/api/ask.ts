import { createFileRoute } from "@tanstack/react-router";
import { searchWeb } from "@/lib/web-lookup";

export const Route = createFileRoute("/api/ask")({
  server: {
    handlers: {
      POST: async ({ request }) => {
        const body = (await request.json()) as { q?: string };
        const q = String(body.q ?? "").trim();
        const result = await searchWeb(q);
        return Response.json(result);
      },
    },
  },
});
