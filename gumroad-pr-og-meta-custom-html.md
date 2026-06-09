## The issue (Given / When / Then)

**Given** a published product that uses a custom‑HTML landing page (`custom_html` present, `custom_html_pages` enabled for the seller),
**When** a crawler or a link‑preview / SEO validator fetches the product page at `/l/:permalink`,
**Then** the `<head>` contains only `og:title`, `og:type`, `og:url`, and `og:image` — and is **missing** `og:description`, `fb:app_id`, and every `twitter:*` tag that a standard product page emits.

The practical result: a shared link shows a title and image but **no description text**, and OpenGraph/SEO validators report *"missing required property `og:description` / `fb:app_id`."*

### "Isn't `og:description` taken from the product description? Does setting that not work?"

It is — and no, it doesn't, when `custom_html` is present. This product has a **1,692‑char description set** and `/l/joucal` still emits no `og:description`. Same account, both products have descriptions; the only difference is `custom_html`:

```
curl -s https://lackluster.gumroad.com/l/paketti | grep -c 'property="og:description"'   # 1  (standard page)
curl -s https://lackluster.gumroad.com/l/joucal  | grep -c 'property="og:description"'   # 0  (custom_html page)
```

### Why it happens

`og:description` is set in `PageMeta::Product#set_open_graph_meta` (from `plaintext_description`). But a custom‑HTML product short‑circuits before that runs. `LinksController`:

```ruby
before_action :render_custom_html_if_present, only: [:show]
```

`render_custom_html_if_present` renders `custom_html_wrapper_document(...)` and returns, so the normal `show` path (which calls `set_product_page_meta`) never executes. And `custom_html_wrapper_document` hand‑builds its own `<head>` — only `og:title` / `og:type` / `og:url` / `og:image`. So for custom‑HTML products the description‑derived `og:description` (plus `fb:app_id` and `twitter:*`) is silently dropped.

## How this fixes it (Given / When / Then)

**Given** the same custom‑HTML product,
**When** `custom_html_wrapper_document` builds the wrapper `<head>`,
**Then** it now also emits `name="description"`, `og:description`, `fb:app_id`, `twitter:card`, `twitter:title`, `twitter:description`, and `twitter:image` — derived from the product **exactly as `PageMeta::Product` already does**:

- description = `product.plaintext_description` (or `"Available on Gumroad"` when blank), truncated to 200 chars;
- `twitter:card` = `summary_large_image` when the product has a thumbnail, else `summary`;
- all values `ERB::Util.h`‑escaped, the same as the existing `og:image` / `og:title` / canonical tags.

Custom‑HTML landing pages reach **parity** with standard product pages for social/SEO meta. The standard Inertia product page and the sandboxed iframe content document (`/l/:id/landing/embed`) are untouched.

## Tests

- `spec/controllers/links_controller_custom_html_spec.rb` — the wrapper carries `og:description` / `fb:app_id` / `twitter:*` from the description, plus the no‑description fallback.
- `spec/requests/products/show/custom_html_meta_tags_spec.rb` — **full‑stack `GET /l/:id`** for a custom_html product with a description asserts the tags are present in the served HTML, plus the fallback. This one **fails on `main`** (the wrapper omitted them) and **passes with the fix** — the end‑to‑end proof that `custom_html`, not a missing description, is what dropped the tags.

## Before → After (`/l/:permalink` `<head>` for a custom‑HTML product)

```diff
  <meta property="og:title"  content="…">
+ <meta name="description"   content="…">
+ <meta property="fb:app_id" content="…">
+ <meta property="og:description" content="…">
  <meta property="og:type"   content="product">
  <meta property="og:url"    content="…">
  <meta property="og:image"  content="…">
+ <meta property="twitter:card" content="summary_large_image">
+ <meta property="twitter:title" content="…">
+ <meta property="twitter:description" content="…">
+ <meta property="twitter:image" content="…">
```
