**Re: og:description on Gumroad product pages — can the CLI / pages emit it?**

Hi Sahil — thanks again, the `--file` upload tip was spot on. ParaEQ is live and I now build → package → upload → publish entirely from the CLI.

One thing I can't solve from the CLI *or* the dashboard: **my product page has no `og:description`** (and no `twitter:*` / `fb:app_id`). When someone shares the link, the preview shows my title and image but **no description text**, and social/SEO validators flag *"missing required property og:description."*

**What the page actually emits** — `curl https://lackluster.gumroad.com/l/joucal`:

```
og:title, og:type, og:url, og:image   ← that is the complete set
```

**What I tried — none of these produce `og:description`:**

- `gumroad products update --description "<html>"` → sets the on-page body, not og:description.
- `gumroad products update --custom-summary "<plain text>"` → no og:description either.
- `gumroad products page publish <id> ./landing.html` with `<meta property="og:description">` in the file → the sanitizer **strips every `<meta>`**. Confirmed via `gumroad products page preview <id> ./landing.html`: no `<meta>` survives in the returned `custom_html`.

So today there is genuinely **no path — CLI or HTML — for a seller to set the description that appears in link previews.** On a landing page, that's the one line that sells the click.

**The ask (any one of these solves it):**

1. Emit `og:description` on product pages, derived from the product **description** or **custom_summary** (plain-text, truncated ~200 chars). This alone clears the validator.
2. Bonus: also emit `twitter:card=summary_large_image`, `twitter:title`, `twitter:description`, `twitter:image`, and `og:image:width`/`og:image:height`.
3. CLI surface: a `--og-description` flag (or treat `--custom-summary` as the og:description source), and have `products page publish` preserve a small head-`<meta>` allowlist (`og:*`, `twitter:*`, `fb:app_id`).
4. Optional account-level `fb:app_id` for sellers who want to clear strict validators / use FB domain insights.

Happy to test any of this against ParaEQ (`lackluster.gumroad.com/l/joucal`) — I can verify the rendered tags with a curl in seconds.

Thanks!
Esa
