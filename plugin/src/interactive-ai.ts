// ── Interactive AI — independent API call for the Play button ──

interface InteractiveEntry {
  type: "user" | "ai" | "interactive";
  text: string;
  timestamp: string;
}

export class InteractiveAI {
  private apiKey: string;
  private baseUrl: string;
  private model: string;

  constructor(apiKey: string, baseUrl: string, model = "gemini-2.5-flash") {
    this.apiKey = apiKey;
    this.baseUrl = baseUrl.replace(/\/+$/, "");
    this.model = model;
  }

  async generate(recentEntries: InteractiveEntry[], currentTime: string): Promise<string> {
    const systemPrompt = `你是 Pinclaw Interactive AI。用户按了 Play 按钮，主动请求你说话。

规则：
- 你能看到最近的对话记录和时间戳，根据时间差自行判断该说什么
- 1分钟内：聚焦未回答的问题或补充信息
- 1-5分钟：提炼要点、新角度
- 5分钟以上：新想法、建议、或引导性问题
- 没有记录时：问一个好问题
- 回复 1-3 句话，不超过
- 不重复 AI 已说的内容
- 不自我介绍，不说"好的/收到"
- 用户用什么语言你就用什么语言
- 绝不沉默，没东西说就问问题`;

    const userContent = this.formatEntries(recentEntries, currentTime);

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 15_000);

    try {
      const res = await fetch(`${this.baseUrl}/v1/chat/completions`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${this.apiKey}`,
        },
        body: JSON.stringify({
          model: this.model,
          messages: [
            { role: "system", content: systemPrompt },
            { role: "user", content: userContent },
          ],
          max_tokens: 200,
          temperature: 0.7,
        }),
        signal: controller.signal,
      });

      if (!res.ok) {
        const body = await res.text().catch(() => "");
        throw new Error(`API ${res.status}: ${body.slice(0, 200)}`);
      }

      const data = (await res.json()) as {
        choices?: { message?: { content?: string } }[];
      };
      const content = data.choices?.[0]?.message?.content?.trim();
      if (!content) throw new Error("Empty response from API");
      return content;
    } finally {
      clearTimeout(timeout);
    }
  }

  private formatEntries(entries: InteractiveEntry[], currentTime: string): string {
    if (!entries.length) {
      return `（没有最近的对话记录）\n\n当前时间: ${currentTime}`;
    }

    const lines = entries.map((e) => {
      const ts = e.timestamp.slice(11, 16); // "HH:mm"
      const label = e.type === "user" ? "用户" : e.type === "interactive" ? "Interactive" : "AI";
      return `[${ts}] ${label}: ${e.text}`;
    });

    return `${lines.join("\n")}\n\n当前时间: ${currentTime}`;
  }
}
