extern crate parser;

#[no_mangle]
pub extern "C" fn asdf() {
    println!("Hiiiii!");
    println!("{:?}", parser::parse_script("2+2"));
}
#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
