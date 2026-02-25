# odin-protoc-plugin
![build status](https://github.com/lordhippo/odin-protoc-plugin/actions/workflows/build.yml/badge.svg)

A Protobuf compiler [plugin](https://protobuf.dev/reference/other/#plugins) for Odin. It should be used with the runtime [odin-protobuf](https://github.com/lordhippo/odin-protobuf) library.

## Usage
Put this plugin's binary next to a [Protobuf Compiler (protoc)](https://github.com/protocolbuffers/protobuf) or in the PATH. Then use the `--odin_out` option to generate Odin files.

## Sample output
For this example proto file:

```proto
message SearchRequest {
  string query = 1;
  int32 page_number = 2;
  int32 results_per_page = 3;
}
```

This plugin will generate this Odin output:
```odin
SearchRequest :: struct {
  query : string `id:"1" type:"9"`,
  page_number : i32 `id:"2" type:"5"`,
  results_per_page : i32 `id:"3" type:"5"`,
}
```

## Missing features
- [Default values](https://github.com/lordhippo/odin-protoc-plugin/issues/5)
- [Dependencies](https://github.com/lordhippo/odin-protoc-plugin/issues/7)
- [Oneof *WhichOneof* check](https://github.com/lordhippo/odin-protoc-plugin/issues/6)

## Working with unions

`oneof` fields are mapped to Odin unions. The generator selects between a tagged union and a C-style `#raw_union` based on type distinctness and explicit annotations.

### Tagged unions

A tagged union is generated when all effective field types inside the `oneof` are distinct. Distinctness is evaluated after applying any explicit type overrides via the `(odin).external` extension.

The following message will therefore generate the following code:
```proto
message SystemRequest {
  int32 command_id = 1;
  oneof payload {
    ReadRequest read_request = 2;
    WriteRequest write_request = 3;
  }
}

message ReadRequest {
  string path = 1;
}
message WriteRequest {}
```
```odin
SystemRequest :: struct {
  command_id : i32 `id:"1" type:"5"`,
  payload: union {
    ReadRequest,
    WriteRequest,
  },
}

ReadRequest  :: struct {
  path: string `id:"1" type:"9"`,
}
WriteRequest :: struct {}

// ...extra boilerplate code
```

Tagged unions may contain message types or scalar types, provided that all resulting union members are distinct.

### Explicit type overrides

Fields can be annotated with `(odin).external` to control which Odin type is used inside a tagged union:

```proto
message UserAction {
  oneof payload {
    string login_with_username  = 1 [(odin).external = "LoginAction"];
    string logout_with_username = 2 [(odin).external = "LogoutAction"];
  }
}
```
```odin
UserAction :: struct {
  payload: union {
    LoginAction,
    LogoutAction,
  },
}
// ...extra boilerplate code
```
> [!WARNING]
> The generated code above assumes that `LoginAction` and `LogoutAction` types are defined and accessible in this
> package at the same location as the generated file. If they are missing, trying to compile this
> code will fail..
>
> What we can do instead is instruct the generator to generate type aliases for us through the `[(odin).typedef]`
> annotation. Note that this annotation cannot combined with `[(odin).external]`, which only
> references a supposedly already existing type:
```proto
message UserAction {
  oneof payload {
    string login_with_username  = 1 [(odin).typedef = "LoginAction"];
    string logout_with_username = 2 [(odin).typedef = "LogoutAction"];
  }
}
```
Which will generate the following aliases for us:
```odin
// exact same union as before
UserAction :: struct {
  payload: union {
    LoginAction,
    LogoutAction,
  },
}

// generated aliases using the backing field types
LoginAction  :: distinct string
LogoutAction :: distinct string
```

Rules for `(odin).external`:

- The annotation forces generation of a tagged union.
- After applying overrides, all union member types must be distinct, so the following will not work
as they share the same generated types.
  ```proto
    oneof res {
      bytes a = 1;
      bytes b = 2 [(odin).external = "[]u8"];
    };
  ```
  You could instead generate an alias through `[(odin).typedef]` and specify a custom type name.
- Duplicate effective types result in a compilation error.

### Raw unions and explicit discriminants

A C-style `#raw_union` is generated **only when effective field types in a `oneof` are not distinct** and no `(odin).external`/`(odin).typedef` annotations are present to force generation of a tagged union.  

In this case, an explicit discriminator field is generated alongside the union. The discriminator is named `<oneof_name>_variant` and is an enum listing all field names in the `oneof`.  

```proto
message UserAction {
  oneof content {
    int32 set_user_id   = 1;
    string set_hostname = 2;
    int32 set_other_id  = 3;
  }
}
```
```odin
UserAction :: struct {
  content : struct #raw_union {
    set_user_id  : i32 `id:"1" type:"5"`,
    set_hostname : string `id:"2" type:"9"`,
    set_other_id : i32 `id:"3" type:"5"`,
  },
  content_variant : enum {
    set_user_id  = 0,
    set_hostname = 1,
    set_other_id = 2,
  },
}
```

This fallback ensures correctness when tagged union constraints cannot be satisfied without explicit user intent.

### Summary of generation rules

- If all effective field types are distinct → generate tagged union.
- If duplicates exist and no `(odin).external` is used → generate `#raw_union` with discriminator.
- If any field uses `(odin).external` → tagged union is required.
- If forced tagged union still contains duplicate types → generation fails.
- Empty `(odin).external` values are invalid.
